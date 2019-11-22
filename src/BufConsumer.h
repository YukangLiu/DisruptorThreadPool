#pragma once
#include "Disruptor.h"

namespace Kang {

	//利用RAII封装Disruptor的读操作：
	//构造的时候调用GetReadableSeq()获取可读序号
	//使用empty()判断是否有可读内容
	//使用GetContent()获取buffer中的内容
	//析构的时候调用FinishReading()交还disruptor中ringbuffer的槽位使用权
	//
	//使用实例：
	//	std::function<void()> task;
	//	{
	//     BufConsumer<std::function<void()>> consumer(this->_tasks);
	//     if (consumer.empty())
	//     {
	//	     return;
	//     }
	//     task = std::move(consumer.GetContent());
	//	}
	//	task();
	template<class ValueType>
	class BufConsumer
	{
	public:

		BufConsumer(Disruptor<ValueType>* disruptor) : _disruptor(disruptor), _seq(-1L) {
			_seq = _disruptor->GetReadableSeq();
		};

		~BufConsumer()
		{
			_disruptor->FinishReading(_seq);
		};

		BufConsumer(const BufConsumer&) = delete;
		BufConsumer(const BufConsumer&&) = delete;
		void operator=(const BufConsumer&) = delete;

		bool empty()
		{
			return _seq < 0;
		}

		ValueType& GetContent()
		{
			return _disruptor->ReadFromBuf(_seq);
		}

	private:
		Disruptor<ValueType>* _disruptor;
		int64_t _seq;
	};

}