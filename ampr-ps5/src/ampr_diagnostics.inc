// SPDX-License-Identifier: GPL-3.0-or-later
// Coordinator samples existing counters. It never consumes a ninth thread.
static Json diagnosticTimeline;
static uint64_t diagnosticStarted=0,diagnosticLast=0;
static Json diagnosticCounters(){auto&d=pipelineDiagnostics;return Json{
 {"readBytes",ioMetrics.readBytes.load()},{"writeBytes",ioMetrics.writeBytes.load()},
 {"readNs",ioMetrics.readNs.load()},{"writeNs",ioMetrics.writeNs.load()},{"workerNs",ioMetrics.workerNs.load()},
 {"readerIdleNs",d.readerIdle.load()},{"readerBlockedNs",d.readerBlocked.load()},{"readerOrderNs",d.readerOrder.load()},
 {"workerIdleNs",d.workerIdle.load()},{"writerIdleNs",d.writerIdle.load()},
 {"coordinatorInputNs",d.coordinatorInput.load()},{"coordinatorOutputNs",d.coordinatorOutput.load()},{"coordinatorFinalizeNs",d.coordinatorFinalize.load()},
 {"readerTaskNs",d.readerTask.load()},{"writerTaskNs",d.writerTask.load()},
 {"readerQueue",d.readerQueue.load()},{"workerQueue",d.workerQueue.load()},
 {"writerQueuedBytes",d.writerBytes.load()},{"packingQueue",d.packQueue.load()}};}
static void diagnosticSample(bool force=false){if(!pipelineDiagnostics.enabled)return;auto now=ioClock();if(!force&&now-diagnosticLast<100000000)return;auto sample=diagnosticCounters();sample["seconds"]=double(now-diagnosticStarted)/1e9;diagnosticTimeline.push_back(std::move(sample));diagnosticLast=now;if(diagnosticTimeline.size()>32){Json reduced=Json::array();for(size_t i=0;i<diagnosticTimeline.size();i+=2)reduced.push_back(diagnosticTimeline[i]);if(reduced.back()!=diagnosticTimeline.back())reduced.push_back(diagnosticTimeline.back());diagnosticTimeline=std::move(reduced);}}
static void diagnosticBegin(){directReadStats.reset();benchmarkDirectReads=true;pipelineDiagnostics.reset();diagnosticTimeline=Json::array();diagnosticStarted=ioClock();diagnosticLast=0;diagnosticSample(true);}
static Json diagnosticEnd(double seconds,StageThreads split){diagnosticSample(true);pipelineDiagnostics.enabled=false;benchmarkDirectReads=false;auto counters=diagnosticCounters();double ns=seconds*1e9;
 auto pct=[&](const char*key,unsigned threads){return threads?std::min(100.0,counters[key].get<double>()/ns/threads*100):0.0;};
 Json fractions{{"readerIoPercent",pct("readNs",split.readers)},{"readerIdlePercent",pct("readerIdleNs",split.readers)},
 {"readerBackpressurePercent",pct("readerBlockedNs",split.readers)},{"readerOrderWaitPercent",pct("readerOrderNs",split.readers)},
 {"workerBusyPercent",pct("workerNs",split.workers)},{"workerInputWaitPercent",pct("workerIdleNs",split.workers)},
 {"writerServicePercent",pct("writerTaskNs",split.writers)},{"writerInputWaitPercent",pct("writerIdleNs",split.writers)},
 {"coordinatorInputWaitPercent",pct("coordinatorInputNs",1)},{"coordinatorOutputWaitPercent",pct("coordinatorOutputNs",1)},{"coordinatorFinalizePercent",pct("coordinatorFinalizeNs",1)}};
 double ci=fractions["coordinatorInputWaitPercent"],co=fractions["coordinatorOutputWaitPercent"];
 fractions["coordinatorOtherPercent"]=std::max(0.0,100-ci-co-fractions["coordinatorFinalizePercent"].get<double>());
 Json findings=Json::array();
 if(fractions["coordinatorFinalizePercent"].get<double>()>20)findings.push_back("Coordinator final flush/fsync is a large part of this sample. Small files and flush latency can dominate packing scores.");
 if(fractions["readerBackpressurePercent"].get<double>()>20||co>20)findings.push_back("Downstream backpressure observed: producers wait for bounded output capacity. Inspect writer service time and queue occupancy.");
 if(split.workers&&fractions["workerInputWaitPercent"].get<double>()>25)findings.push_back("Processing workers wait for input. Read service time, reader publication order, and coordinator scheduling may limit supply.");
 if(split.writers&&fractions["writerInputWaitPercent"].get<double>()>25)findings.push_back("Writers wait for data. The upstream read/process path or fixed file/archive ownership may limit supply.");
 if(fractions["readerOrderWaitPercent"].get<double>()>20)findings.push_back("Readers wait to publish in source order; unequal read latency or a slower preceding batch may hold later batches.");
 if(split.workers&&fractions["workerBusyPercent"].get<double>()>80)findings.push_back("Processing is heavily occupied; additional processing capacity may help if input/output can keep up.");
 if(findings.empty())findings.push_back("No dominant measured wait. Review the timeline; startup, metadata, flushes, caches and short samples can dominate.");
 Json direct{{"attempts",directReadStats.attempts.load()},{"flagAccepted",directReadStats.accepted.load()},{"rejected",directReadStats.rejected.load()},{"completedReads",directReadStats.completed.load()},{"readFallbacks",directReadStats.fallbacks.load()},{"alignedReadBytes",directReadStats.bytes.load()},{"lastErrno",directReadStats.lastError.load()},{"hardwareCacheBypassVerified",false}};
 return Json{{"directRead",direct},{"seconds",seconds},{"counters",counters},{"percentages",fractions},{"findings",findings},{"timeline",diagnosticTimeline},
 {"limitations","Wait and service totals are summed across role threads and normalized by role capacity. Read service includes filesystem/cache time, not hardware device utilization. Queue values are periodic snapshots, not time-weighted averages. Open waits finish when their scope exits; timeline deltas can bunch at wakeups. Coordinator other time includes setup, metadata, polling, scheduling and flushes. Findings are hypotheses, not causal proof."}};
}
