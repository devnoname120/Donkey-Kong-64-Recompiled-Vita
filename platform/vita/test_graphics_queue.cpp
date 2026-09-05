#include "ultramodern/graphics_queue.hpp"
#include <iostream>
#include <future>
#include <thread>

static std::pair<int,int> crossThreadOrder(bool ordered) {
    moodycamel::BlockingConcurrentQueue<int> queue;
    ultramodern::OrderedGraphicsProducer<decltype(queue)> producer(queue);
    std::promise<void> graphics_ready,readback_ready,release;
    auto wait=release.get_future().share();
    // Keep both threads alive so the implicit-producer case cannot reuse the
    // first thread's retired producer and accidentally appear globally ordered.
    std::thread graphics([&] {
        if(ordered) producer.enqueue(1); else queue.enqueue(1);
        graphics_ready.set_value(); wait.wait();
    });
    graphics_ready.get_future().wait();
    std::thread readback([&] {
        if(ordered) producer.enqueue(2); else queue.enqueue(2);
        readback_ready.set_value(); wait.wait();
    });
    readback_ready.get_future().wait();
    std::pair<int,int> result{};
    queue.try_dequeue(result.first); queue.try_dequeue(result.second);
    release.set_value(); graphics.join(); readback.join();
    return result;
}

int main() {
    if(crossThreadOrder(false)!=std::pair<int,int>{2,1}) return 11;
    if(crossThreadOrder(true)!=std::pair<int,int>{1,2}) return 12;
    std::cout<<"Graphics queue: reproduced cross-thread readback overtaking; ordered producer preserved task/readback order\n";
    ultramodern::LatestPresentation<int> presentation;
    if(!presentation.publish(1)) return 7;
    for(int i=2;i<=10000;++i) if(presentation.publish(i)) return 8;
    if(presentation.consume()!=10000 || !presentation.publish(10001)) return 9;
    if(presentation.publish(10002) || presentation.consume()!=10002) return 10;
    using Queue=moodycamel::BlockingConcurrentQueue<int,ultramodern::VitaGraphicsQueueTraits>;
    Queue queue;
    moodycamel::ProducerToken vi(queue),graphics(queue);
    for(int i=0;i<15;++i) queue.enqueue(vi,0);
    queue.enqueue(graphics,1);
    // Reproduce a paced renderer: every consumed VI is replaced by a new VI.
    for(int i=0;i<120;++i) {
        int value=-1;
        if(!queue.try_dequeue(value) || value!=0) return 1;
        queue.enqueue(vi,0);
    }
    moodycamel::ConsumerToken consumer(queue);
    bool first=false;
    for(int i=0;i<4;++i) {
        int value=-1;
        if(!queue.wait_dequeue_timed(consumer,value,1000)) return 2;
        if(value==1) {
            first=true;
            break;
        }
        queue.enqueue(vi,0);
    }
    if(!first) return 3;
    // The real renderer keeps its consumer token across tasks. Exercise the
    // fallback into a permanently nonempty VI stream before another task comes.
    for(int i=0;i<8;++i) {
        int value=-1;
        if(!queue.wait_dequeue_timed(consumer,value,1000)||value!=0) return 4;
        queue.enqueue(vi,0);
    }
    queue.enqueue(graphics,2);
    for(int i=0;i<8;++i) {
        int value=-1;
        if(!queue.wait_dequeue_timed(consumer,value,1000)) return 5;
        if(value==2) {
            std::cout<<"Graphics queue: reproduced heuristic starvation; reusable consumer delivered the later task after "<<i+1<<" dequeues\n";
            return 0;
        }
        queue.enqueue(vi,0);
    }
    return 6;
}
