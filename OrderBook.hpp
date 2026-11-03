#pragma once
#include <bits/stdc++.h>
using namespace std;
#include "Order.hpp"
#include <condition_variable>
#include <functional>
#include <list>
#include <mutex>
#include <thread>

// One OrderBook per symbol. Each book owns a worker thread that drains a task queue.
//
// Two mutexes, never held at the same time:
//   queue_mtx - guards the task queue and the done/busy flags (producers <-> worker)
//   book_mtx  - guards the book state itself (worker executing a task <-> printers)
// Producers only ever touch queue_mtx, so enqueueing never waits behind matching work.
class OrderBook
{
private:
    // Where a resting order lives. A std::list iterator stays valid when *other*
    // elements are erased, so cancel/fill can erase in O(1) with no scan.
    struct OrderRef
    {
        Side side;
        int level;                     // price*100 + paise
        list<Order>::iterator it;
    };

    // ---- book state (guarded by book_mtx) ----
    map<int, list<Order>, greater<int>> buy_orders;
    map<int, list<Order>> sell_orders;
    unordered_map<int, OrderRef> order_list;
    vector<vector<int>> transaction_list;
    mutex book_mtx;

    // ---- task queue (guarded by queue_mtx) ----
    queue<function<void()>> tasks;
    bool done{false};
    bool busy{false};                  // worker is between "popped a task" and "finished it"
    condition_variable cv;             // worker waits here for work / shutdown
    condition_variable idle_cv;        // flush() waits here for the queue to drain
    mutex queue_mtx;

    thread worker;                     // declared last: starts only after everything above exists

    static void print_order(const Order& order)
    {
        cout<<"Order ID: "<<order.get_order_ID()<<", Price: "<<order.get_price()<<"."<<((order.get_price_paise()>=10)?"":"0")<<order.get_price_paise()<<", Quantity: "<<order.get_quantity()<<", Type: "<<(order.get_type() == OrderType::limit ? "Limit" : "Market")<<", Side: "<<(order.get_side() == Side::buy ? "Buy" : "Sell")<<", TIF: ";
        if(order.get_tif() == TIF::GTC) cout<<"GTC";
        else if(order.get_tif() == TIF::IOC) cout<<"IOC";
        else if(order.get_tif() == TIF::FOK) cout<<"FOK";
        cout<<'\n';
    }

    // Caller must hold book_mtx.
    template <class Levels>
    static void print_side(const char* title, const char* rule, const Levels& levels)
    {
        if(levels.empty())
        {
            return;
        }
        cout << title << '\n';
        cout << rule << '\n';
        for(const auto& level : levels)
        {
            for(const auto& order : level.second)
            {
                print_order(order);
            }
        }
    }

    void process()
    {
        unique_lock<mutex> lock(queue_mtx);
        while(true)
        {
            cv.wait(lock, [this] { return !tasks.empty() || done; });
            if(tasks.empty())
                break;                  // done && nothing left to drain
            function<void()> task = std::move(tasks.front());
            tasks.pop();
            busy = true;
            lock.unlock();
            {
                lock_guard<mutex> book_lock(book_mtx);
                task();
            }
            lock.lock();
            busy = false;
            if(tasks.empty())
                idle_cv.notify_all();
        }
    }

public:
    OrderBook() : worker([this] { process(); }) {}

    // Owns a mutex, condition variables and a running thread that captured `this`.
    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    // The only place the worker is stopped: finish every queued task, then join.
    ~OrderBook()
    {
        {
            lock_guard<mutex> lock(queue_mtx);
            done = true;
        }
        cv.notify_all();
        if(worker.joinable())
            worker.join();
    }

    void add_task(function<void()> task)
    {
        {
            lock_guard<mutex> lock(queue_mtx);
            tasks.push(std::move(task));
        }
        cv.notify_one();
    }

    // Blocks until every task enqueued before this call has finished. The worker keeps
    // running afterwards. print_book()/transaction_history() call this first so that a
    // print reflects every order submitted before it (the old code got that effect by
    // killing the worker via shutdown()).
    void flush()
    {
        unique_lock<mutex> lock(queue_mtx);
        idle_cv.wait(lock, [this] { return tasks.empty() && !busy; });
    }

    // Thread-safe snapshot print. Skips books with no resting orders.
    void print_book(const string& symbol)
    {
        flush();
        lock_guard<mutex> lock(book_mtx);
        if(buy_orders.empty() && sell_orders.empty())
        {
            return;
        }
        cout << "\n-------------------------------------------------" << '\n';
        cout << "\nORDER BOOK FOR SYMBOL: " << symbol << '\n'<<'\n';
        print_side("Buy Orders:", "------------", buy_orders);
        print_side("Sell Orders:", "-------------", sell_orders);
    }

    void transaction_history()
    {
        flush();
        lock_guard<mutex> lock(book_mtx);
        for (const auto& transaction : transaction_list)
        {
            cout << "Buy Order ID: " << transaction[0] << ", Sell Order ID: " << transaction[1] << ", Quantity: " << transaction[2] <<", Price: "<<transaction[3]<<"."<< (transaction[4]>=10?"":"0")<<transaction[4]<< '\n';
        }
    }

    // NOT synchronized: these run on the worker thread with book_mtx already held,
    // so reach them only through add_task().
    void add_order(Order order);
    void remove_order(int order_ID);
};

void OrderBook::add_order(Order order)
{
    int val = order.get_price() * 100 + order.get_price_paise();

    if (order.get_side() == Side::buy)
    {
        if (order.get_type() == OrderType::limit)
        {
            // FOK: Check if enough sell quantity is available
            if (order.get_tif() == TIF::FOK)
            {
                int cur = 0;
                for (auto it = sell_orders.begin(); it != sell_orders.end() && it->first <= val; it++)
                {
                    for (auto& temp : it->second)
                    {
                        cur += temp.get_quantity();
                        if (cur >= order.get_quantity()) break;
                    }
                    if (cur >= order.get_quantity()) break;
                }
                if (cur < order.get_quantity())
                {
                    cout << "Order cannot be placed as it is FOK and not enough quantity available\n";
                    return ;
                }
            }

            // Try to match with sell orders
            for (auto it = sell_orders.begin(); it != sell_orders.end() && it->first <= val && order.get_quantity() > 0;)
            {
                while (!it->second.empty() && order.get_quantity() > 0)
                {
                    auto& sell_order = it->second.front();
                    int m = min(sell_order.get_quantity(), order.get_quantity());
                    order.reduce_quantity(m);
                    sell_order.reduce_quantity(m);
                    int price = min(sell_order.get_price(), order.get_price());
                    int price_paise;
                    if (sell_order.get_price_paise() < order.get_price_paise())
                        price_paise = sell_order.get_price_paise();
                    else
                        price_paise = order.get_price_paise();
                    transaction_list.push_back({order.get_order_ID(), sell_order.get_order_ID(), m, price, price_paise});
                    if (sell_order.get_quantity() == 0)
                    {
                        order_list.erase(sell_order.get_order_ID());
                        it->second.pop_front();
                    }
                }
                if (it->second.empty()) it = sell_orders.erase(it);
                else it++;
            }

            if (order.get_quantity() > 0 && order.get_tif() != TIF::IOC)
            {
                auto& level = buy_orders[val];
                level.push_back(order);
                order_list[order.get_order_ID()] = OrderRef{Side::buy, val, prev(level.end())};
                cout<<"Order added with ID: "<<order.get_order_ID()<<'\n';
                return ;
            }
            else
            {
                if (order.get_quantity() > 0)
                    cout << "Order Partially Executed, Remaining Demand cannot be placed as it is IOC and not enough quantity available\n";
                else
                    cout << "Order was fully executed\n";
                return ;
            }
        }
        else // Market Buy
        {
            if (order.get_tif() != TIF::IOC && order.get_tif() != TIF::FOK)
            {
                cout << "Invalid TIF for market order. Only IOC or FOK allowed.\n";
                return ;
            }

            if (order.get_tif() == TIF::FOK)
            {
                int cur = 0;
                for (auto it = sell_orders.begin(); it != sell_orders.end(); it++)
                {
                    for (auto& sell_order : it->second)
                    {
                        cur += sell_order.get_quantity();
                        if (cur >= order.get_quantity()) break;
                    }
                    if (cur >= order.get_quantity()) break;
                }
                if (cur < order.get_quantity())
                {
                    cout << "Order cannot be placed as it is FOK and not enough quantity available\n";
                    return ;
                }
            }

            for (auto it = sell_orders.begin(); it != sell_orders.end() && order.get_quantity() > 0;)
            {
                while (!it->second.empty() && order.get_quantity() > 0)
                {
                    auto& sell_order = it->second.front();
                    int m = min(sell_order.get_quantity(), order.get_quantity());
                    order.reduce_quantity(m);
                    sell_order.reduce_quantity(m);
                    int price = min(sell_order.get_price(), order.get_price());
                    int price_paise;
                    if (sell_order.get_price_paise() < order.get_price_paise())
                        price_paise = sell_order.get_price_paise();
                    else
                        price_paise = order.get_price_paise();
                    transaction_list.push_back({order.get_order_ID(), sell_order.get_order_ID(), m, price, price_paise});
                    if (sell_order.get_quantity() == 0)
                    {
                        order_list.erase(sell_order.get_order_ID()); // before pop_front: pop destroys sell_order
                        it->second.pop_front();
                    }
                }
                if (it->second.empty()) it = sell_orders.erase(it);
                else it++;
            }

            if (order.get_quantity() > 0)
                cout << "Order Partially Executed, Remaining Demand cannot be placed as it is IOC and not enough quantity available\n";
            else
                cout << "Order was fully executed\n";
            return ;
        }
    }

    else if (order.get_side() == Side::sell)
    {
        if (order.get_type() == OrderType::limit)
        {
            if (order.get_tif() == TIF::FOK)
            {
                int cur = 0;
                for (auto it = buy_orders.begin(); it != buy_orders.end() && it->first >= val; it++)
                {
                    for (auto& temp : it->second)
                    {
                        cur += temp.get_quantity();
                        if (cur >= order.get_quantity()) break;
                    }
                    if (cur >= order.get_quantity()) break;
                }
                if (cur < order.get_quantity())
                {
                    cout << "Order cannot be placed as it is FOK and not enough quantity available\n";
                    return ;
                }
            }

            for (auto it = buy_orders.begin(); it != buy_orders.end() && it->first >= val && order.get_quantity() > 0;)
            {
                while (!it->second.empty() && order.get_quantity() > 0)
                {
                    auto& buy_order = it->second.front();
                    int m = min(buy_order.get_quantity(), order.get_quantity());
                    order.reduce_quantity(m);
                    buy_order.reduce_quantity(m);
                    int price = min(buy_order.get_price(), order.get_price());
                    int price_paise;
                    if (buy_order.get_price_paise() < order.get_price_paise())
                        price_paise = buy_order.get_price_paise();
                    else
                        price_paise = order.get_price_paise();
                    transaction_list.push_back({ buy_order.get_order_ID(),order.get_order_ID(), m, price, price_paise});
                    if (buy_order.get_quantity() == 0)
                    {
                        order_list.erase(buy_order.get_order_ID());
                        it->second.pop_front();
                    }
                }
                if (it->second.empty()) it = buy_orders.erase(it);
                else it++;
            }

            if (order.get_quantity() > 0 && order.get_tif() != TIF::IOC)
            {
                auto& level = sell_orders[val];
                level.push_back(order);
                order_list[order.get_order_ID()] = OrderRef{Side::sell, val, prev(level.end())};
                cout<<"Order added with ID: "<<order.get_order_ID()<<'\n';
                return ;
            }
            else
            {
                if (order.get_quantity() > 0)
                    cout << "Order Partially Executed, Remaining Demand cannot be placed as it is IOC and not enough quantity available\n";
                else
                    cout << "Order was fully executed\n";
                return ;
            }
        }

        else // Market Sell
        {
            if (order.get_tif() != TIF::IOC && order.get_tif() != TIF::FOK)
            {
                cout << "Invalid TIF for market order. Only IOC or FOK allowed.\n";
                return ;
            }

            if (order.get_tif() == TIF::FOK)
            {
                int cur = 0;
                for (auto it = buy_orders.begin(); it != buy_orders.end(); it++)
                {
                    for (auto& buy_order : it->second)
                    {
                        cur += buy_order.get_quantity();
                        if (cur >= order.get_quantity()) break;
                    }
                    if (cur >= order.get_quantity()) break;
                }
                if (cur < order.get_quantity())
                {
                    cout << "Order cannot be placed as it is FOK and not enough quantity available\n";
                    return ;
                }
            }

            for (auto it = buy_orders.begin(); it != buy_orders.end() && order.get_quantity() > 0;)
            {
                while (!it->second.empty() && order.get_quantity() > 0)
                {
                    auto& buy_order = it->second.front();
                    int m = min(buy_order.get_quantity(), order.get_quantity());
                    order.reduce_quantity(m);
                    buy_order.reduce_quantity(m);
                    int price = min(buy_order.get_price(), order.get_price());
                    int price_paise;
                    if (buy_order.get_price_paise() < order.get_price_paise())
                        price_paise = buy_order.get_price_paise();
                    else
                        price_paise = order.get_price_paise();
                    transaction_list.push_back({buy_order.get_order_ID(),order.get_order_ID(), m, price, price_paise});
                    if (buy_order.get_quantity() == 0)
                    {
                        order_list.erase(buy_order.get_order_ID()); // before pop_front: pop destroys buy_order
                        it->second.pop_front();
                    }
                }
                if (it->second.empty()) it = buy_orders.erase(it);
                else it++;
            }

            if (order.get_quantity() > 0)
                cout << "Order Partially Executed, Remaining Demand cannot be placed as it is IOC and not enough quantity available\n";
            else
                cout << "Order was fully executed\n";
            return ;
        }
    }

    return ; // fallback
}


void OrderBook::remove_order(int order_ID)
{
    auto found = order_list.find(order_ID);
    if (found == order_list.end())
    {
        cout << "Order not found\n";
        return;
    }
    const OrderRef ref = found->second;
    order_list.erase(found);
    if (ref.side == Side::buy)
    {
        auto level = buy_orders.find(ref.level);
        level->second.erase(ref.it);
        if (level->second.empty())
        {
            buy_orders.erase(level);
        }
    }
    else
    {
        auto level = sell_orders.find(ref.level);
        level->second.erase(ref.it);
        if (level->second.empty())
        {
            sell_orders.erase(level);
        }
    }
    cout << "Order removed successfully\n";
}
