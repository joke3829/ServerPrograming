#include <iostream>
#include <thread>
#include <vector>
#include <mutex>

std::mutex sm;

void worker()
{
	sm.lock();
	std::cout << std::this_thread::get_id() << std::endl;
	sm.unlock();
}

int main()
{
	std::vector<std::thread> my_thread;
	for (int i = 0; i < 10; ++i)
		my_thread.emplace_back(worker);
	for (auto& t : my_thread) t.join();
}