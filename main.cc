#include <iostream>
#include <thread>
#include <chrono>
#include <signal.h>

#include "context.h"
#include "chan.h"
#include "go.h"


void show(const Context& ctx) {
	std::cout << ctx->value("name") << std::endl;
	std::cout << ctx->value("age") << std::endl;
}

auto done = make_chan<int>();

void handler(int) {
	std::cout << "cancel request" << std::endl;
    done->close();
}

void test1() {
	Context f = background();
	Context s = with_value(f, "name", "allen");
	Context d = with_value(s, "age", "7");
    show(d);
}

void test2() {
	auto ch1 = make_chan<int, 1>();
	auto ch2 = make_chan<int>(); // no cache

	std::thread t1([ch1, ch2]() {
        std::cout << std::boolalpha << (*ch1 << 100) << std::endl;
		std::cout << "push 100" << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(2));
		*ch1 << 200;
		std::cout << "push 200" << std::endl;

		std::this_thread::sleep_for(std::chrono::seconds(2));
		*ch2 << 300;
		std::cout << "push 300" << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(2));
		*ch2 << 400;
		std::cout << "push 400" << std::endl;
	});

	int x;
	std::cout << "ready to recv from ch1 after 5s" << std::endl;
	std::this_thread::sleep_for(std::chrono::seconds(5));
	std::cout << "ready to recv from ch1" << std::endl;
    std::cout << std::boolalpha << (*ch1 >> x) << std::endl;
	std::cout << x << std::endl;
	*ch1 >> x;
	std::cout << x << std::endl;

	std::cout << "ready to recv from ch2 after 1s" << std::endl;
	std::this_thread::sleep_for(std::chrono::seconds(1));
	std::cout << "ready to recv from ch2" << std::endl;
	*ch2 >> x;
	std::cout << x << std::endl;
	*ch2 >> x;
	std::cout << x << std::endl;

    t1.join();
}

void test3() {
    auto request = make_chan<int, 1>();
    std::thread t1([request]{
        while(true) {
            std::cout << "wait response" << std::endl;
            auto active = choose({*done, *request});
            if (active[0]) {
                std::cout << "cancel" << std::endl;
                request->close();
                break;
            }

            if (active[1]) {
                int x;
                *request >> x;
                std::cout << "request done:" << x << std::endl;
                break;
            }

        }
    });

    std::thread t2([request]{
        std::this_thread::sleep_for(std::chrono::seconds(3));
        std::cout << "request << 9999:" << std::boolalpha << (*request << 9999) << std::endl;
    });
	t1.join();
    t2.join();
}

void test4() {
    auto b = background();
    auto c = with_cancel(b);
    auto ctx = std::get<0>(c);
    auto cancel = std::get<1>(c);
    Go<>([ctx]{
        auto c1 = with_cancel(ctx);
        auto c2 = with_cancel(ctx);

        Go<decltype(c1)>([](decltype(c1) c1){
            auto ctx1 = std::get<0>(c1);
            auto active = choose({ctx1->done()});
            if (active[0]) {
                std::cout << "canceled c1" << std::endl;
            }
        })(c1);

        Go<>([c2]{
            auto ctx2 = std::get<0>(c2);
            auto active = choose({ctx2->done()});
            if (active[0]) {
                std::cout << "canceled c2" << std::endl;
            }
        })();

        auto active = choose({ctx->done()});
        if (active[0]) {
            std::cout << "canceled c" << std::endl;
        }
    })();

    std::cout << "cancel after 3s" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(3));
    cancel();
}

void test5() {
    Go<int, int>([](int a, int b) {
        std::cout << "a = " << a << "; b = " << b << std::endl;
    })(1, 2);
}

int main() {
    signal(SIGINT, handler);
    //test1();
    //test2();
    //test3();
    test4();
    test5();

    std::this_thread::sleep_for(std::chrono::seconds(3));
    std::cout << "---------- end ----------" << std::endl;
	return 0;
}
