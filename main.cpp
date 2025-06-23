#include<bits/stdc++.h>
using namespace std;
#include "Order.hpp"
#include "OrderBook.hpp"
#include "Exchange.hpp"
signed main()
{
    Exchange ex;
    while(true)
    {
        cout<<"\n";
        int choice;
        cin >> choice;
        if (cin.fail() || choice < 1 || choice > 5) 
        {
            cin.clear(); 
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            // cout << "Invalid choice. Please enter a number between 1 and 4.\n";
            continue;
        }

        if (choice == 5) break;
        else if(choice==4)
        {
            ex.print_order_books();
            continue;
        }
        string symbol;
        // cout << "\nEnter Stock Name: ";
        cin >> symbol;

        if (choice == 1)
        {
            int id, price, price_paise, quantity;
            string type_str, side_str, tif_str;
            // cout << "\nEnter price, Decimal part of price([0,99]), type (limit/market), side (buy/sell), quantity(>0), TIF (GTC/IOC/FOK): ";
            cin >> price >> price_paise >> type_str >> side_str >> quantity >> tif_str;
            if(cin.fail()||price<0||price_paise<0||(price+price_paise)==0||price_paise>=100||quantity<=0||(type_str != "limit" && type_str != "market") || (side_str != "buy" && side_str != "sell") || (tif_str != "GTC" && tif_str != "IOC" && tif_str != "FOK"))
            {
                cin.clear(); 
                cin.ignore(numeric_limits<streamsize>::max(), '\n'); // Ignore bad input
                // cout << "Invalid input. Please enter valid values.\n";
                continue;
            }
            id=ex.get_next_order_id();
            OrderType type = (type_str == "limit") ? OrderType::limit : OrderType::market;
            Side side = (side_str == "buy") ? Side::buy : Side::sell;
            TIF tif = (tif_str == "GTC") ? TIF::GTC : (tif_str == "IOC") ? TIF::IOC : TIF::FOK;

            Order order(id, price, price_paise, type, side, quantity, tif);
            ex.add_order(symbol, order);
        }
        else if (choice == 2)
        {
            int order_ID;
            // cout << "Enter order ID to remove: ";
            cin >> order_ID;
            if(cin.fail() || order_ID < 0)
            {
                cin.clear(); // Clear error flags
                cin.ignore(numeric_limits<streamsize>::max(), '\n'); // Ignore bad input
                // cout << "Invalid order ID. Please enter a valid positive integer.\n";
                continue;
            }
            ex.remove_order(symbol, order_ID);
        }
        else if (choice == 3)
        {
            ex.transaction_history(symbol);
        }
    }
}