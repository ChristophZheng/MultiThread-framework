#pragma once
#include <atomic>
#include <unordered_map>
#include <sys/timeb.h>
#include <functional>
struct period_cal
{
	unsigned int id;
	unsigned long second_last_cal;
	unsigned long millis_last_cal;
	unsigned long millis_period;
	int times_to_cal; /*-1: infinite >=0: exactly the times*/
	std::function<void()> caller;
};

class crt_periodic_cal
{
private:
	void regist(const period_cal &c)
	{
		std::lock_guard<std::mutex> lk(m);
		if (c.times_to_cal == -1 || c.times_to_cal > 0)
		{
			period_cals.insert(std::make_pair(c.id, c));
		}
	}
	void unregist(int id)
	{
		std::lock_guard<std::mutex> lk(m);
		auto itr = period_cals.find(id);
		if (itr != period_cals.cend())
		{
			period_cals.erase(itr);
		}
	}

	void run()
	{
		at_b_work.store(true);
		while (at_b_work.load())
		{
			timeb t;
			ftime(&t);
			
			unsigned long millis_to_wait = 1000;
			m.lock();
			auto itr = period_cals.begin();
			while (itr != period_cals.end())
			{
				auto &cal = (*itr).second;
				unsigned long millis_passed = (t.time - cal.second_last_cal) * 1000 +
					(t.millitm >= cal.millis_last_cal ? t.millitm - cal.millis_last_cal : 1000 - (cal.millis_last_cal - t.millitm));
				if (millis_passed >= cal.millis_period)
				{
					if (cal.times_to_cal > 0 && --cal.times_to_cal == 0)
					{
						to_remove.push(cal.id);
					}
					cal.caller();
					cal.millis_last_cal = t.millitm;
					cal.second_last_cal = t.time;
					if (millis_to_wait < cal.millis_period)
					{
						millis_to_wait = cal.millis_period;
					}
				}
				else
				{
					unsigned long millis_next_shot = cal.millis_period - millis_passed;
					if (millis_next_shot < millis_to_wait)
					{
						millis_to_wait = millis_next_shot;
					}
				}
				++itr;
			}

			while (!to_remove.empty())
			{
				period_cals.erase(period_cals.find(to_remove.front()));
				to_remove.pop();
			}
			m.unlock();
			std::this_thread::sleep_for(std::chrono::milliseconds(millis_to_wait));
		}
	}
	void stop()
	{
		at_b_work.store(false);
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
private:
	friend class crt_frame;
	std::atomic_bool at_b_work;
	std::mutex m;
	std::unordered_map<int, period_cal> period_cals;
	std::queue<int> to_remove;

};