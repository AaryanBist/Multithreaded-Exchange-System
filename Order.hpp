#include <bits/stdc++.h>
#pragma once
using namespace std;
enum class OrderType{limit,market};
enum class Side {buy,sell};
enum class TIF{GTC,IOC,FOK};
class Order
{
    int order_ID;
    int price;
    int price_paise;
    OrderType type;
    Side side;
    int quantity;
    TIF tif;;
    public:
        Side get_side() const { return side; }
        OrderType get_type() const { return type; }
        int get_price() const { return price; }
        int get_price_paise() const { return price_paise; }
        int get_quantity() const { return quantity; }
        void reduce_quantity(int q) { quantity -= q; }
        int get_order_ID() const { return order_ID; }
        TIF get_tif() const { return tif; }
        Order(int id,double pr,int pp,OrderType t,Side s,int q,TIF ti):order_ID(id),price(pr),price_paise(pp),type(t),side(s),quantity(q),tif(ti){}
};
