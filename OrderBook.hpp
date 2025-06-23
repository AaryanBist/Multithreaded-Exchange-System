#include <bits/stdc++.h>
#pragma once
using namespace std;
#include "Order.hpp"
#include    <mutex>
#include <condition_variable>
#include <functional>
class OrderBook
{
private:
    map<int, deque<Order>, greater<int>> buy_orders;
    queue<function<void()>> tasks;
    map<int, deque<Order>> sell_orders;
    map<int, Order*> order_list;
    queue<Order>q;
    vector<vector<int>> transaction_list;
    int order_count{1};
    bool done{false};
    condition_variable cv;
    mutex mtx;
    thread worker;
public:
    void add_order(Order order);
    void remove_order(int order_ID);
    void print_buy_orders() const
    {
        if(buy_orders.empty())
        {
            return;
        }
        cout << "Buy Orders:\n";
        cout<< "------------\n";
        for(auto price:buy_orders)
        {
            for(auto order:price.second)
            {
                cout<<"Order ID: "<<order.get_order_ID()<<", Price: "<<order.get_price()<<"."<<((order.get_price_paise()>=10)?"":"0")<<order.get_price_paise()<<", Quantity: "<<order.get_quantity()<<", Type: "<<(order.get_type() == OrderType::limit ? "Limit" : "Market")<<", Side: "<<(order.get_side() == Side::buy ? "Buy" : "Sell")<<", TIF: "; 
                if(order.get_tif() == TIF::GTC) cout<<"GTC";
                else if(order.get_tif() == TIF::IOC) cout<<"IOC";
                else if(order.get_tif() == TIF::FOK) cout<<"FOK";
                cout<<'\n';
            }
        }
    }
    void print_sell_orders() const
    {
        if(sell_orders.empty())
        {
            return;
        }
        cout << "Sell Orders:\n";
        cout<< "-------------\n";
        for(auto price:sell_orders)
        {
            for(auto order:price.second)
            {
                cout<<"Order ID: "<<order.get_order_ID()<<", Price: "<<order.get_price()<<"."<<((order.get_price_paise()>=10)?"":"0")<<order.get_price_paise()<<", Quantity: "<<order.get_quantity()<<", Type: "<<(order.get_type() == OrderType::limit ? "Limit" : "Market")<<", Side: "<<(order.get_side() == Side::buy ? "Buy" : "Sell")<<", TIF: "; 
                if(order.get_tif() == TIF::GTC) cout<<"GTC";
                else if(order.get_tif() == TIF::IOC) cout<<"IOC";
                else if(order.get_tif() == TIF::FOK) cout<<"FOK";
                cout<<'\n';
            }
        }
    }
    void transaction_history() const
    {
        for (const auto& transaction : transaction_list)
        {
            cout << "Buy Order ID: " << transaction[0] << ", Sell Order ID: " << transaction[1] << ", Quantity: " << transaction[2] <<", Price: "<<transaction[3]<<"."<< (transaction[4]>=10?"":"0")<<transaction[4]<< '\n';
        }
    }
    int is_empty() const
    {
        return buy_orders.empty() && sell_orders.empty();
    }
    void process()
    {
        while(true)
        {
            function<void()> task;
            {
            unique_lock<mutex> lock(mtx);
            cv.wait(lock, [this] { return !tasks.empty() || done; });
            if (done && tasks.empty())
                 break;
            if(!tasks.empty())
            {
                task = tasks.front();
                tasks.pop();
            }
            }
            if(task)
                task();
        }
    }
    OrderBook()
    {
        worker=thread([this] {process();});
    }
    ~OrderBook()
    {
        {
            lock_guard<mutex> lock(mtx);
            done = true; // Set done FIRST
        }
        cv.notify_all(); // Wake up worker
        
        {
            unique_lock<mutex> lock(mtx);
            cv.wait(lock, [this] { return tasks.empty(); }); // Then wait for empty
        }
        
        if(worker.joinable())
            worker.join();
    }
    void add_task(function<void()> task)
    {
        bool need_restart = false;
        {
            lock_guard<mutex> lock(mtx);
            tasks.push(task);
            if(done) {
                done = false;
                need_restart = true;
            }
        }
        if(need_restart) {
            worker = thread([this] {process();});
        }
        cv.notify_one();
    }
    void shutdown()
    {
        {
            lock_guard<std::mutex> lock(mtx);
            done = true;
        }
        cv.notify_all();
        {
            unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, [this]{ return tasks.empty(); });
        }

        if (worker.joinable())
            worker.join();
    }   

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
                buy_orders[val].push_back(order);
                order_list[order.get_order_ID()] = &buy_orders[val].back();
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
                        it->second.pop_front();
                        order_list.erase(sell_order.get_order_ID());
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
                sell_orders[val].push_back(order);
                order_list[order.get_order_ID()] = &sell_orders[val].back();
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
                        it->second.pop_front();
                        order_list.erase(buy_order.get_order_ID());
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
    auto order = order_list.find(order_ID);
    if (order == order_list.end())
    {
        cout << "Order not found\n";
        return;
    }
    int val = order->second->get_price() * 100 + order->second->get_price_paise();
    if (order->second->get_side() == Side::buy)
    {
        for (auto it = buy_orders[val].begin(); it != buy_orders[val].end(); it++)
        {
            if (it->get_order_ID() == order_ID)
            {
                buy_orders[val].erase(it);
                order_list.erase(order_ID);
                if (buy_orders[val].empty())
                {
                    buy_orders.erase(val);
                }
                cout << "Order removed successfully\n";
                return;
            }
        }
    }
    else if (order->second->get_side() == Side::sell)
    {
        for (auto it = sell_orders[val].begin(); it != sell_orders[val].end(); it++)
        {
            if (it->get_order_ID() == order_ID)
            {
                sell_orders[val].erase(it);
                order_list.erase(order_ID);
                if (sell_orders[val].empty())
                {
                    sell_orders.erase(val);
                }
                cout << "Order removed successfully\n";
                return;
            }
        }
    }
}
