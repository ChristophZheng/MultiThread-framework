#pragma once
#include <queue>
#include <mutex>
#include <memory>
#include <future>
class message_base
{
public:
	virtual ~message_base(){}
};

template <class T>
class wrapped_message : public message_base
{
public:
	template <typename T_>
	wrapped_message(T_ && c) : content(std::forward<T_>(c)) {}
	wrapped_message& operator=(const wrapped_message &other) = delete;
	wrapped_message(const wrapped_message &other) = delete;
	const T content;
};
class msg_quit{};
class msg_timer_activate{};
class task_base
{
public:
	virtual ~task_base(){}
};

template <class T>
class wrapped_task : public task_base
{
public:
	const T task;
	template <typename T_>
	wrapped_task(T_ && t) : task(std::forward<T_>(t)){}
	wrapped_task& operator=(const wrapped_task &other) = delete;
	wrapped_task(const wrapped_task &other) = delete;
	auto operator()()->decltype(task())
	{
		return task();
	}
};


class crt_threadBase
{
protected:
	friend class crt_frame;
	virtual ~crt_threadBase(){}
	std::queue<std::shared_ptr<message_base>> hdl_msg;
	std::mutex m;
	std::condition_variable cond;
};

using Type_character_id = decltype(typeid(crt_threadBase).hash_code());
template <class Type_TaskRet>
class crt_thread : public crt_threadBase
{
protected:
	virtual void msg_handle(const message_base *p_msg) = 0;
	virtual void timer_handle() = 0;
private:
	void run()
	{
		for (;;)
		{
			std::unique_lock<std::mutex> lk(m);
			bool b_new_msg = false;
			bool b_new_task = false;
			cond.wait(lk, 
			[&]()->bool
			{
				if (!hdl_msg.empty())
				{
					b_new_msg = true;
				}
				if (!hdl_task.empty())
				{
					b_new_task = true;
				}
				return b_new_msg || b_new_task;
			}
			);
			std::shared_ptr<message_base> p_msg;
			if(b_new_msg)
			{
				p_msg = hdl_msg.front();
				hdl_msg.pop();
			}
			std::packaged_task<Type_TaskRet()> task;
			if (b_new_task)
			{
				task = std::move(hdl_task.front());
				hdl_task.pop();
			}
			lk.unlock();

			if(b_new_msg)
			{
				message_base *p = p_msg.get();
				if (dynamic_cast<wrapped_message<msg_quit>*>(p))
				{
					break;
				}
				else if (dynamic_cast<wrapped_message<msg_timer_activate>*>(p))
				{
					timer_handle();
				}
				else
				{
					msg_handle(p);
				}
			}

			if (b_new_task)
			{
				task();
			}
			
		}
	}
	friend class crt_frame;
	std::queue<std::packaged_task<Type_TaskRet()>> hdl_task;
};