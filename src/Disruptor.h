#pragma once
#include <atomic>
#include <array>

#define CACHELINE_SIZE_BYTES 64
#define CACHELINE_PADDING_FOR_ATOMIC_INT64_SIZE (CACHELINE_SIZE_BYTES - sizeof(std::atomic_int64_t))
#define CACHELINE_PADDING_FOR_INT64_SIZE (CACHELINE_SIZE_BYTES - sizeof(int64_t))

namespace Kang {

	//对std::atomic_int64_t进行了封装，内存补齐保证_seq在一个缓存行中
	class AtomicSequence
	{
	public:
		AtomicSequence(int64_t num = 0L) : _seq(num) {};
		~AtomicSequence() {};
		AtomicSequence(const AtomicSequence&) = delete;
		AtomicSequence(const AtomicSequence&&) = delete;
		void operator=(const AtomicSequence&) = delete;

		void store(const int64_t val)//, std::memory_order _order = std::memory_order_seq_cst)
		{
			_seq.store(val);//,_order);
		}

		int64_t load()//std::memory_order _order = std::memory_order_seq_cst)
		{
			return _seq.load();// _order);
		}

		int64_t fetch_add(const int64_t increment)//, std::memory_order _order = std::memory_order_seq_cst)
		{
			return _seq.fetch_add(increment);// _order);
		}

	private:
		//两边都补齐，以保证_seq不会与其它变量共享一个缓存行
		char _frontPadding[CACHELINE_PADDING_FOR_ATOMIC_INT64_SIZE];
		std::atomic_int64_t _seq;
		char _backPadding[CACHELINE_PADDING_FOR_ATOMIC_INT64_SIZE];
	};

	//对int64_t进行了封装，内存补齐保证_seq在一个缓存行中
	class Sequence
	{
	public:
		Sequence(int64_t num = 0L) : _seq(num) {};
		~Sequence() {};
		Sequence(const Sequence&) = delete;
		Sequence(const Sequence&&) = delete;
		void operator=(const Sequence&) = delete;

		void store(const int64_t val)
		{
			_seq = val;
		}

		int64_t load()
		{
			return _seq;
		}

	private:
		//两边都补齐，以保证_seq不会与其它变量共享一个缓存行
		char _frontPadding[CACHELINE_PADDING_FOR_INT64_SIZE];
		int64_t _seq;
		char _backPadding[CACHELINE_PADDING_FOR_INT64_SIZE];
	};

	//环形buffer默认大小,为了简化取余操作，这里的长度需要是2的n次方
	constexpr size_t DefaultRingBufferSize = 262144;

	//该disruptor类提供对环形buffer的操作
	//写接口：WriteInBuf()
	//读接口：(1)GetReadableSeq()获取可读的环形buffer槽位下标
	//        (2)ReadFromBuf()获取可读的内容
	//        (3)读完后调用FinishReading()
	//注：读接口使用复杂，使用了BufConsumer类进行了封装（利用RAII）
	template<class ValueType, size_t N = DefaultRingBufferSize>
	class Disruptor
	{
	public:
		Disruptor() : _lastRead(-1L) , _lastWrote(-1L), _lastDispatch(-1L), _writableSeq(0L) , _stopWorking(0L){};
		~Disruptor() {};

		Disruptor(const Disruptor&) = delete;
		Disruptor(const Disruptor&&) = delete;
		void operator=(const Disruptor&) = delete;

		static_assert(((N > 0) && ((N& (~N + 1)) == N)),
			"RingBuffer's size must be a positive power of 2");

		//向buffer中写内容
		void WriteInBuf(ValueType&& val)
		{
			const int64_t writableSeq = _writableSeq.fetch_add(1);
			while (writableSeq - _lastRead.load() > N)
			{//等待策略
				if (_stopWorking.load())
					throw std::runtime_error("writting when stopped disruptor");
				//std::this_thread::yield();
			}
			//写操作
			_ringBuf[writableSeq & (N - 1)] = val;

			while (writableSeq - 1 != _lastWrote.load())
			{//等待策略
			}
			_lastWrote.store(writableSeq);
		};

		//向buffer中写内容
		void WriteInBuf(ValueType& val)
		{
			const int64_t writableSeq = _writableSeq.fetch_add(1);
			while (writableSeq - _lastRead.load() > N)
			{//等待策略
				if (_stopWorking.load())
					throw std::runtime_error("writting when stopped disruptor");
				//std::this_thread::yield();
			}
			//写操作
			_ringBuf[writableSeq & (N - 1)] = val;

			while (writableSeq - 1 != _lastWrote.load())
			{//等待策略
			}
			_lastWrote.store(writableSeq);
		};

		//获取可读的buffer下标
		const int64_t GetReadableSeq()
		{
			const int64_t readableSeq = _lastDispatch.fetch_add(1) + 1;
			while (readableSeq > _lastWrote.load())
			{//等待策略
				if (_stopWorking.load() && empty())
				{
					return -1L;
				}
			}
			return readableSeq;
		};

		//读取指定下标位置的buffer内容
		ValueType& ReadFromBuf(const int64_t readableSeq)
		{
			if (readableSeq < 0)
			{
				throw("error : incorrect seq for ring Buffer when ReadFromBuf(seq)!");
			}
			return _ringBuf[readableSeq & (N - 1)];
		}

		//读取完指定下标位置的buffer内容，交还下标位置使用权
		void FinishReading(const int64_t seq)
		{
			if (seq < 0)
			{
				return;
			}

			while (seq - 1 != _lastRead.load())
			{//等待策略
			}
			//_lastRead = seq;
			_lastRead.store(seq);
		};

		bool empty()
		{
			return _writableSeq.load() - _lastRead.load() == 1;
		}

		//通知disruptor停止工作，调用该函数后，若buffer已经全部处理完，那么获取可读下标时只会获取到-1L
		void stop()
		{
			//_stopWorking = true;
			_stopWorking.store(1L);
		}

	private:
		//最后一个已读内容位置
		Sequence _lastRead;

		//最后一个已写内容位置
		Sequence _lastWrote;

		//disruptor是否停止工作
		Sequence _stopWorking;

		//最后一个派发给消费者使用的槽位序号
		AtomicSequence _lastDispatch;

		//当前可写的槽位序号
		AtomicSequence _writableSeq;

		//环形buffer，为加快取余操作，N需要时2的n次幂
		std::array<ValueType, N> _ringBuf;
	};

}
