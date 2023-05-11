#include<iostream>
using namespace std;
int changeA(int &a){
    a = 4;
    return a;
}
int main()
{
    int a = 3;
    changeA(a);
    cout << a << endl;
    return 0;
}