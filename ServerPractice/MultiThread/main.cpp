#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <mutex>

std::mutex sm;

void worker(volatile int& sum)
{
	for (int i = 0; i < 50000000 / 2; ++i) {
		sm.lock();
		sum = sum + 2;
		sm.unlock();
	}
}

int main()
{
	using namespace std::chrono;

	{
		volatile int sum = 0;
		auto start_t = high_resolution_clock::now();
		for (int i = 0; i < 50000000; ++i)
			sum = sum + 2;
		auto end_t = high_resolution_clock::now();
		auto exec_t = end_t - start_t;
		auto ms = duration_cast<milliseconds>(exec_t).count();

		std::cout << "SingleThread Sum : " << sum << ", Time - " << ms << "ms" << std::endl;
	}

	{
		volatile int sum = 0;
		auto start_t = high_resolution_clock::now();
		std::thread t1{ worker , std::ref(sum)};
		std::thread t2{ worker , std::ref(sum)};
		t1.join(); t2.join();
		auto end_t = high_resolution_clock::now();
		auto exec_t = end_t - start_t;
		auto ms = duration_cast<milliseconds>(exec_t).count();

		std::cout << "2 Thread Sum : " << sum << ", Time - " << ms << "ms" << std::endl;
	}
}