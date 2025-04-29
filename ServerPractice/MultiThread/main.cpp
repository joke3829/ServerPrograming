#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <mutex>
#include <atomic>

std::mutex sm;

void worker(std::atomic_int& sum, const int num_threads)
{
	for (int i = 0; i < 50000000 / num_threads; ++i) {
		sum += 2;
	}
}

int main()
{
	using namespace std::chrono;

	/*{
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
	}*/
	std::vector<std::thread> threads;
	for (int i = 1; i <= 16; i *= 2) {
		threads.clear();
		std::atomic_int sum = 0;
		auto start_t = high_resolution_clock::now();
		for (int j = 0; j < i; ++j) {
			threads.emplace_back(worker, std::ref(sum), i);
		}
		for (auto& t : threads)
			t.join();
		auto end_t = high_resolution_clock::now();
		auto exec_t = end_t - start_t;
		auto ms = duration_cast<milliseconds>(exec_t).count();
		std::cout << i << "Threads Sum: " << sum << ", time - " << ms << "ms" << std::endl;
	}
	int n{};
}