#pragma once
#include "crt_thread_base.h"
#include "crt_periodic_cal.h"

struct thread_unit
{
	thread_unit() :at_thread_character_id(0), at_p_thread_obj(nullptr), at_referring_count(0){}
	std::atomic<Type_character_id> at_thread_character_id;
	std::atomic<void*> at_p_thread_obj;
	std::atomic<unsigned int> at_referring_count;
};

#define MAX_Threads 32
class crt_frame
{
public:
	class Exp_ThdNotFound{};
	class Exp_ThdDuplicate{};
	class Exp_ThdOverSub{};
	template <typename Thread_T>
	static void load() noexcept(false)
	{
		Type_character_id id = typeid(Thread_T).hash_code();
		std::lock_guard<std::mutex> lk(thd_pl_weak_lk);
		int i = 0;
		for (; i < MAX_Threads && thread_pool[i].at_thread_character_id.load(); ++i)
		{
			if (thread_pool[i].at_thread_character_id.load() == id)
			{
				if (thread_pool[i].at_p_thread_obj.load())
				{
					throw Exp_ThdDuplicate();
				}
				else
				{
					create_at_i<Thread_T>(i);
					return;
				}

			}
		}
		if (i < MAX_Threads)
		{
			thread_pool[i].at_thread_character_id.store(id);
			create_at_i<Thread_T>(i);
		}
		else
		{
			throw Exp_ThdOverSub();
		}
	}

	template <typename Thread_T>
	static void unload() noexcept(false)
	{
		Type_character_id id = typeid(Thread_T).hash_code();
		std::lock_guard<std::mutex> lk(thd_pl_weak_lk);
		int i = -1;
		Thread_T * p_thd_obj = nullptr;
		find_thdPl_index<Thread_T>(i, p_thd_obj);/*throw if not found*/
		thread_pool[i].at_referring_count--;
		thread_pool[i].at_p_thread_obj.store(nullptr);
		while (thread_pool[i].at_referring_count.load()) {};
		unregist_timer<Thread_T>();
		period_cal c;
		c.id = id + 748;
		timeb t;
		ftime(&t);
		c.millis_last_cal = t.millitm;
		c.second_last_cal = t.time;
		c.millis_period = 5000;
		c.times_to_cal = 1;
		c.caller = [p_thd_obj]()->void {delete p_thd_obj;};
		p_period_cal->regist(c);
	}

	template <typename Thread_T>
	static void regist_timer(int millis) noexcept(false)
	{
		Type_character_id id = typeid(Thread_T).hash_code();
		period_cal c;
		c.id = id;
		c.millis_period = millis;
		c.times_to_cal = -1;
		c.caller = []()->void {push_msg<Thread_T>(msg_timer_activate()); };
		timeb t;
		ftime(&t);
		c.millis_last_cal = t.millitm;
		c.second_last_cal = t.time;
		p_period_cal->regist(c);
	}

	template <typename Thread_T>
	static void unregist_timer() noexcept(false)
	{
		Type_character_id id = typeid(Thread_T).hash_code();
		p_period_cal->unregist(id);
	}

	template <typename Thread_T, typename T>
	static bool push_msg(T && msg) noexcept
	{
		int pl_index = -1;
		Thread_T *p_thd_obj = nullptr;
		try
		{
			find_thdPl_index<Thread_T>(pl_index, p_thd_obj);
		}
		catch (...)
		{
			return false;
		}
		/*when you reach here, the thread object is found and protected from destroyed*/
		using type_msg = typename std::remove_reference<T>::type;
		std::shared_ptr<wrapped_message<type_msg>> p_msg;
		try
		{
			p_msg = std::make_shared<wrapped_message<type_msg>>(std::forward<T>(msg));
		}
		catch (std::bad_alloc &e)
		{
			thread_pool[pl_index].at_referring_count--;
			return false;
		}
		p_thd_obj->m.lock();
		p_thd_obj->hdl_msg.push(p_msg);
		p_thd_obj->m.unlock();
		p_thd_obj->cond.notify_one();
		thread_pool[pl_index].at_referring_count--;
		return true;
	}

	template <typename T>
	static void broadcast_msg(T && msg) noexcept
	{
		using type_msg = typename std::remove_reference<T>::type;
		std::shared_ptr<wrapped_message<type_msg>> p_msg;
		try
		{
			p_msg = std::make_shared<wrapped_message<type_msg>>(std::forward<T>(msg));
		}
		catch (std::bad_alloc &e)
		{
			return;
		}
		for (int i = 0; i < MAX_Threads && thread_pool[i].at_thread_character_id.load(); ++i)
		{
			thread_pool[i].at_referring_count++;
			crt_threadBase *p_thd = static_cast<crt_threadBase*>(thread_pool[i].at_p_thread_obj.load());
			if (p_thd)
			{
				p_thd->m.lock();
				p_thd->hdl_msg.push(p_msg);
				p_thd->cond.notify_one();
				p_thd->m.unlock();
			}
			thread_pool[i].at_referring_count--;
		}
	}

	template<typename Thread_T, typename F, typename ...A>
	static decltype(auto) spawn_memfn_task_for(F &&f, A &&...args)
	{
		int pl_index = -1;
		Thread_T *p_thd_obj;
		find_thdPl_index<Thread_T>(pl_index, p_thd_obj);
		/*when you reach here, the thread object is found and protected from destroyed*/
		auto func_obj = std::bind(f, p_thd_obj, std::forward<A>(args)...);
		using typeRet = decltype(func_obj());
		std::packaged_task<typeRet()> packed_task(std::move(func_obj));
		auto future = packed_task.get_future();
		p_thd_obj->m.lock();
		p_thd_obj->hdl_task.push(std::move(packed_task));
		p_thd_obj->cond.notify_one();
		p_thd_obj->m.unlock();
		thread_pool[pl_index].at_referring_count--;
		return future;
	}

	template<typename Thread_T, typename F, typename ...A>
	static decltype(auto) spawn_gfn_task_for(F &&f, A &&...args)
	{
		int pl_index = -1;
		Thread_T *p_thd_obj = nullptr;
		find_thdPl_index<Thread_T>(pl_index, p_thd_obj);
		/*when you reach here, the thread object is found and protected from destroyed*/
		auto func_obj = std::bind(f, std::forward<A>(args)...);
		using typeRet = decltype(func_obj());
		std::packaged_task<typeRet()> packed_task(std::move(func_obj));
		auto future = packed_task.get_future();
		p_thd_obj->m.lock();
		p_thd_obj->hdl_task.push(std::move(packed_task));
		p_thd_obj->cond.notify_one();
		p_thd_obj->m.unlock();
		thread_pool[pl_index].at_referring_count--;
		return future;
	}

	template<typename F, typename ...A>
	static decltype(auto) spawn_task_newthd(F &&f, A&& ...args)
	{
		auto func_obj = std::bind(f, std::forward<A>(args)...);
		using typeRet = decltype(func_obj());
		std::packaged_task<typeRet()> packed_task(std::move(func_obj));
		auto future = packed_task.get_future();
		std::thread t(std::move(packed_task));
		t.detach();
		return future;
	}

	template<typename F, typename ...A>
	static decltype(auto) spawn_task_balanced(F &&f, A&& ...args)
	{
		auto future = std::async(f, std::forward<A>(args)...);
		return futrue;
	}
private:
	template<typename Thread_T>
	static void find_thdPl_index(int &pl_index, Thread_T* &p_obj) noexcept(false)
	{
		
		Type_character_id id = typeid(Thread_T).hash_code();
		for (int i = 0; i < MAX_Threads && thread_pool[i].at_thread_character_id.load(); ++i)
		{
			if (thread_pool[i].at_thread_character_id == id)
			{
				thread_pool[i].at_referring_count++;
				p_obj = static_cast<Thread_T*>(thread_pool[i].at_p_thread_obj.load());
				if (p_obj)
				{
					pl_index = i;
					return;
				}
				else
				{
					pl_index = -1;
					thread_pool[i].at_referring_count--;
					throw Exp_ThdNotFound();
				}
			}
		}
		throw Exp_ThdNotFound();
	}

	template<typename Thread_T>
	static void create_at_i(int index) noexcept(false)
	{
		thread_pool[index].at_referring_count.store(0);
		Thread_T *p_thd_obj = new Thread_T;
		std::thread t(&Thread_T::run, p_thd_obj);
		t.detach();
		thread_pool[index].at_p_thread_obj.store(p_thd_obj);
	}

	static crt_periodic_cal* init();
private:
	static thread_unit thread_pool[MAX_Threads];
	static std::mutex thd_pl_weak_lk;
    static crt_periodic_cal *p_period_cal;
};