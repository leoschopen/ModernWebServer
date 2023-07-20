# 定时器模块
HeapTimer 是一个基于堆的定时器模块，用于管理多个定时任务，按照指定的时间间隔执行回调函数。它支持添加、删除、调整和清空定时任务，以及获取下一个定时任务的时间间隔。

## 实现原理
HeapTimer 的实现原理是基于堆排序算法。它将每个定时任务封装成一个 TimerNode 对象，并存储在一个 std::vector 容器中。定时任务的到期时间是 TimerNode 对象的一个属性，按照到期时间构建小根堆。

在添加、删除和调整定时任务时，HeapTimer 会重新调整小根堆，以保证堆的性质。在执行定时任务时，HeapTimer 会遍历堆中所有到期的定时任务，并依次执行它们的回调函数。如果定时任务还未到期，则停止遍历，等待下一个定时任务的到期时间。

## 实现细节
首先typedef定义了四个变量
```c++
typedef std::function<void()> TimeoutCallBack;
//这个定义将 std::function 模板实例化为一个无参无返回值的函数类型 TimeoutCallBack。可以用它来定义定时任务到期时要执行的回调函数类型。传统写法为typedef void (*TimeoutCallBack)();

//例如，在 HeapTimer 类中，定时任务的回调函数就是这样定义的：TimeoutCallBack cb。

typedef std::chrono::high_resolution_clock Clock;
//这个定义将 std::chrono::high_resolution_clock 实例化为一个名为 Clock 的类型。它是 C++ 标准库中的一个高精度时钟，用于测量时间间隔。

//例如，在 HeapTimer 类中，定时任务的到期时间就是使用 Clock::now() 获取的。

typedef std::chrono::milliseconds MS;
//这个定义将 std::chrono::milliseconds 实例化为一个名为 MS 的类型。它是 C++ 标准库中的一个时间间隔类型，用于表示毫秒数。

//例如，在 HeapTimer 类中，定时任务的到期时间是使用 MS(timeout) 构造出来的。

typedef Clock::time_point TimeStamp;
//这个定义将 Clock::time_point 实例化为一个名为 TimeStamp 的类型。它是 Clock 类的一个成员类型，用于表示时间点。

//例如，在 HeapTimer 类中，定时任务的到期时间就是使用 TimeStamp 类型表示的。
```