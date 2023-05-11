//单例设计模式demo
#include <iostream>
#include <mutex>
#include <thread>
using namespace std;

class Singleton{
private:
    Singleton(){
        cout << "创建成功！" << endl;
    }
public:
    static Singleton &getInstance(){
        static Singleton instance;
        return instance;
    }
};

void threadFunc(){
    Singleton &singleton = Singleton::getInstance();
}

int main(){
    //创建3个线程，每个线程都调用getInstance()函数
    thread t1(threadFunc);
    thread t2(threadFunc);
    thread t3(threadFunc);
    t1.join();
    t2.join();
    t3.join();
    return 0;
}