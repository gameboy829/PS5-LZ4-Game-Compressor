// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "http.h"
#include <pthread.h>
#include <functional>
#include <memory>
#include <cstring>
namespace studio {
// The PS5 SDK defaults to a 64 KiB pthread stack. Profile construction and
// coordinator-side parsing need more space than that default provides.
inline void startDetached(std::function<void()> function,size_t stackBytes=8*1024*1024){
    auto job=std::make_unique<std::function<void()>>(std::move(function));
    pthread_attr_t attr;int rc=pthread_attr_init(&attr);
    require(rc==0,"Cannot initialize job thread attributes");
    rc=pthread_attr_setstacksize(&attr,stackBytes);
    if(!rc)rc=pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
    pthread_t thread;
    if(!rc)rc=pthread_create(&thread,&attr,[](void* context)->void*{
        std::unique_ptr<std::function<void()>> owned(static_cast<std::function<void()>*>(context));
        (*owned)();return nullptr;
    },job.get());
    pthread_attr_destroy(&attr);
    require(rc==0,std::string("Cannot start job thread: ")+std::strerror(rc));
    job.release();
}
}
