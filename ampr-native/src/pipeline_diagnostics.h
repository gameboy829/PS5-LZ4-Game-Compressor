// SPDX-License-Identifier: GPL-3.0-or-later
// Benchmark-only observation. No observer threads and no pipeline decisions.
enum class DiagRole { coordinator,reader,worker,writer };
inline thread_local DiagRole diagRole=DiagRole::coordinator;
struct PipelineDiagnostics {
 std::atomic<bool> enabled{false};
 std::atomic<uint64_t> readerIdle{0},readerBlocked{0},readerOrder{0},workerIdle{0},writerIdle{0},coordinatorInput{0},coordinatorOutput{0},coordinatorFinalize{0},readerTask{0},writerTask{0};
 std::atomic<int64_t> readerQueue{0},workerQueue{0},writerBytes{0},packQueue{0};
 void reset(){enabled=false;readerIdle=0;readerBlocked=0;readerOrder=0;workerIdle=0;writerIdle=0;coordinatorInput=0;coordinatorOutput=0;coordinatorFinalize=0;readerTask=0;writerTask=0;readerQueue=0;workerQueue=0;writerBytes=0;packQueue=0;enabled=true;}
};
inline PipelineDiagnostics pipelineDiagnostics;
struct DiagTimer {
 std::atomic<uint64_t>&counter;uint64_t started;
 explicit DiagTimer(std::atomic<uint64_t>&c):counter(c),started(pipelineDiagnostics.enabled?ioClock():0){}
 ~DiagTimer(){if(started)counter+=ioClock()-started;}
};
struct DiagRoleScope {DiagRole previous;explicit DiagRoleScope(DiagRole role):previous(diagRole){diagRole=role;}~DiagRoleScope(){diagRole=previous;}};
inline void diagGauge(std::atomic<int64_t>&g,int64_t delta){if(pipelineDiagnostics.enabled)g+=delta;}
