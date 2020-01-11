#include "stdafx.h"
#include "crt_frame.h"
std::mutex crt_frame::thd_pl_weak_lk;
thread_unit crt_frame::thread_pool[];
crt_periodic_cal* crt_frame::p_period_cal = init();
crt_periodic_cal * crt_frame::init()
{
	crt_periodic_cal *p_period = new crt_periodic_cal;
	std::thread t(&crt_periodic_cal::run, p_period);
	t.detach();
	return p_period;
}
