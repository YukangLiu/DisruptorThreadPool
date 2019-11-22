#pragma once
#include <vector>
#include <memory>
#include <thread>
#include <future>
#include <functional>
#include <stdexcept>

#include "Disruptor.h"
#include "BufConsumer.h"

namespace Kang {

	//Ê¹ÓÃÊ¾Àý£º
	//int main()
	//{

	//	ThreadPool pool(4);
	//	std::vector< std::future<int> > results;

	//	for (int i = 0; i < 8; ++i) {
	//		results.emplace_back(
	//			pool.enqueue([i] {
	//				std::cout << "hello " << i << std::endl;
	//				std::this_thread::sleep_for(std::chrono::seconds(1));
	//				std::cout << "world " << i << std::endl;
	//				return i * i;
	//				})
	//		);
	//	}

	//	for (auto&& result : results)
	//		std::cout << result.get() << ' ';
	//	std::cout << std::endl;

	//	return 0;
	//}
	class ThreadPool {
	public:
		ThreadPool(size_t);
		template<class F, class... Args>
		auto enqueue(F&& f, Args&&... args)
			->std::future<typename std::result_of<F(Args...)>::type>;
		~ThreadPool();
	private:
		// need to keep track of threads so we can join them
		std::vector< std::thread > _workers;

		//Disruptor
		Disruptor<std::function<void()>> *_tasks;

	};

	// the constructor just launches some amount of workers
	inline ThreadPool::ThreadPool(size_t threads)
	{
		_tasks = new Disruptor<std::function<void()>>();
		for (size_t i = 0; i < threads; ++i)
		{
			_workers.emplace_back(
				[this]
				{
					for (;;)
					{
						std::function<void()> task;

						{
							BufConsumer<std::function<void()>> consumer(this->_tasks);
							if (consumer.empty())
							{
								return;
							}
							task = std::move(consumer.GetContent());
						}

						task();
					}
				}
				);
		}
	}

	// add new work item to the pool
	template<class F, class... Args>
	auto ThreadPool::enqueue(F&& f, Args&&... args)
		-> std::future<typename std::result_of<F(Args...)>::type>
	{
		using return_type = typename std::result_of<F(Args...)>::type;

		auto task = std::make_shared< std::packaged_task<return_type()> >(
			std::bind(std::forward<F>(f), std::forward<Args>(args)...)
			);

		std::future<return_type> res = task->get_future();

		_tasks->WriteInBuf(std::move([task]() { (*task)(); }));
		return res;
	}

	// the destructor joins all threads
	inline ThreadPool::~ThreadPool()
	{
		_tasks->stop();
		for (std::thread& worker : _workers)
			worker.join();
		delete _tasks;
	}

}
