#include "threadpool.h"

#include <iostream>
#include <future>
#include <thread>
using namespace std;

using Ulong = unsigned long long;

Ulong func(Ulong a, Ulong b)
{
    Ulong res = 0;
    for (; a < b; ++a)
    {
        res += a;
    }
    return res;
}

int main()
{
    ThreadPool pool;
    pool.start(12);
    clock_t start = clock();
    future<Ulong> res1 = pool.submitTask(func, 1, 1000000000);
    future<Ulong> res2 = pool.submitTask(func, 1000000001, 2000000000);
    future<Ulong> res3 = pool.submitTask(func, 2000000001, 3000000000);
    future<Ulong> res4 = pool.submitTask(func, 3000000001, 4000000000);
    future<Ulong> res5 = pool.submitTask(func, 4000000001, 5000000000);
    future<Ulong> res6 = pool.submitTask(func, 1, 1000000000);
    future<Ulong> res7 = pool.submitTask(func, 1000000001, 2000000000);
    future<Ulong> res8 = pool.submitTask(func, 2000000001, 3000000000);
    future<Ulong> res9 = pool.submitTask(func, 3000000001, 4000000000);
    cout << res1.get() + res2.get() + res3.get() + res4.get() + res5.get() + res6.get() + res7.get() + res8.get() + res9.get() << endl;

    // cout << func(1, 5000000000) << endl;
    cout << clock() - start << endl;
    return 0;
}
