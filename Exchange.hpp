#include<bits/stdc++.h>
#pragma once
using namespace std;
#include "Order.hpp"
#include "OrderBook.hpp"
class Exchange
{
    unordered_map<string, OrderBook> order_books;
    int curr_id{};
    public:
        void add_order(const string& symbol, Order order)
        {
            order_books[symbol].add_task([this, symbol, order](){order_books[symbol].add_order(order);});
        }
        void print_order_books() 
        {
            for( auto& i:order_books)
            {
                i.second.shutdown();
                if(i.second.is_empty())
                {
                    continue;
                }
                cout << "\n-------------------------------------------------" << '\n';
                cout << "\nORDER BOOK FOR SYMBOL: " << i.first << '\n'<<'\n';
                i.second.print_buy_orders();
                i.second.print_sell_orders();
            }
        }
        void remove_order(const string& symbol, int order_ID)
        {
            if (order_books.find(symbol) != order_books.end())
            {
                order_books[symbol].add_task([this,symbol,order_ID]() {
                    order_books[symbol].remove_order(order_ID);
                });
            }
            else
            {
                cout << "Order book for symbol " << symbol << " does not exist.\n";
            }
        }
        int get_next_order_id()
        {
            return curr_id++;
        }

        void transaction_history(const string& symbol) 
        {
            order_books.at(symbol).shutdown();
            if (order_books.find(symbol) != order_books.end())
            {
                order_books.at(symbol).transaction_history();
            }
            else
            {
                cout << "Order book for symbol " << symbol << " does not exist.\n";
            }
        }    
};