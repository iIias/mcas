#include "wrapper_utils.h"
#include "wrapper_cycles.h"
#include "wrapper_cpu.h"

#include <chrono>
#include <iostream>

cpu_time_t ext_clk;

int main(int, char * [])
{
  const auto passes = 100000000UL;
  auto c_t0 = std::chrono::high_resolution_clock::now();
  auto c_t1 = std::chrono::high_resolution_clock::now();
  for ( auto i = 0; i != passes; ++i )
  {
    c_t1 = std::chrono::high_resolution_clock::now();
  }
  auto c_sec = std::chrono::duration<double>(c_t1-c_t0).count();

  auto i_t0 = std::chrono::high_resolution_clock::now();

  cpu_time_t clk = 0;

  for ( auto i = 0; i != passes; ++i )
  {
    clk += rdtsc();
  }
  auto i_sec = std::chrono::duration<double>(std::chrono::high_resolution_clock::now()-i_t0).count();

  ext_clk = clk;

  std::cout << "Average tims in seconds: high_resolution_clock::now: " << c_sec/passes << " rdtsc: " << i_sec/passes << "\n";

  return 0;
}
