#pragma once
#include <bits/stdc++.h>
using namespace std;
#include "Order.hpp"
#include "OrderBook.hpp"
#include <memory>
#include <shared_mutex>
class Exchange
{
    // unique_ptr: a book (mutex + running thread) never moves; the map only ever moves
    // pointers. Books are never removed, so a raw OrderBook* handed out stays valid
    // until the Exchange itself is destroyed.
    unordered_map<string, unique_ptr<OrderBook>> order_books;
    shared_mutex books_mtx;   // guards the map structure only; lookups share, creation is exclusive
    mutex id_mtx;
    int curr_id{};

    // Returns the book for `symbol`, creating it (and its worker thread) on first use.
    OrderBook* get_or_create_book(const string& symbol)
    {
        {
            shared_lock<shared_mutex> lock(books_mtx);
            auto it = order_books.find(symbol);
            if (it != order_books.end())
                return it->second.get();
        }
        unique_lock<shared_mutex> lock(books_mtx);
        auto it = order_books.find(symbol);   // another thread may have created it meanwhile
        if (it == order_books.end())
            it = order_books.emplace(symbol, make_unique<OrderBook>()).first;
        return it->second.get();
    }

    OrderBook* find_book(const string& symbol)
    {
        shared_lock<shared_mutex> lock(books_mtx);
        auto it = order_books.find(symbol);
        return it == order_books.end() ? nullptr : it->second.get();
    }

public:
    void add_order(const string& symbol, Order order)
    {
        // One hash lookup here, on the caller's thread. The worker captures the raw
        // pointer and never touches the map. It cannot dangle: the book's destructor
        // joins its worker, so the book outlives every task queued on it.
        OrderBook* book = get_or_create_book(symbol);
        book->add_task([book, order]() { book->add_order(order); });
    }
    void print_order_books()
    {
        // Snapshot under the shared lock, print outside it, so slow prints don't block
        // creation of new books.
        vector<pair<string, OrderBook*>> snapshot;
        {
            shared_lock<shared_mutex> lock(books_mtx);
            for (auto& entry : order_books)
                snapshot.emplace_back(entry.first, entry.second.get());
        }
        for (auto& [symbol, book] : snapshot)
            book->print_book(symbol);
    }
    void remove_order(const string& symbol, int order_ID)
    {
        OrderBook* book = find_book(symbol);
        if (book)
        {
            book->add_task([book, order_ID]() { book->remove_order(order_ID); });
        }
        else
        {
            cout << "Order book for symbol " << symbol << " does not exist.\n";
        }
    }
    int get_next_order_id()
    {
        lock_guard<mutex> lock(id_mtx);
        return curr_id++;
    }

    void transaction_history(const string& symbol)
    {
        OrderBook* book = find_book(symbol);
        if (book)
        {
            book->transaction_history();
        }
        else
        {
            cout << "Order book for symbol " << symbol << " does not exist.\n";
        }
    }
};
