#pragma once
#include <chrono>
struct IoMetrics {std::atomic<uint64_t> readBytes{0},writeBytes{0},readNs{0},writeNs{0},workerNs{0};std::atomic<unsigned> workers{0},busy{0},readers{0},writers{0};};
inline IoMetrics ioMetrics;
inline uint64_t ioClock(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
struct IoMeasure{std::atomic<uint64_t>&time;uint64_t start;explicit IoMeasure(std::atomic<uint64_t>&n):time(n),start(ioClock()){}~IoMeasure(){time+=ioClock()-start;}};
inline void resetIoMetrics(){ioMetrics.readers=0;ioMetrics.writers=0;ioMetrics.readBytes=0;ioMetrics.writeBytes=0;ioMetrics.readNs=0;ioMetrics.writeNs=0;ioMetrics.workerNs=0;}
