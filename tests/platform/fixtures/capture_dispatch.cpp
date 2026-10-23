#include "latest_frame.h"
#include <atomic>
#include <future>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstring>
LatestFrame g_encoder_frames;
std::atomic<bool> g_stream_requested(true);
unsigned char pixels[32];
struct { uintptr_t vir; } gIonMem;
std::promise<void> entered, release_gate;
std::shared_future<void> released;
int hold_stage;
void hold(int stage) { if(hold_stage==stage) { entered.set_value(); released.wait(); } }
void present_local_frame(uint64_t) { hold(1); }
void queue_snapshot_frame(const unsigned char*) { hold(2); }
void dispatch() {
uint64_t frame_count=17,captured_us=123,captured_mono=456;
#include DISPATCH
}
int main(int argc,char**argv) {
hold_stage=argc>1?argv[1][0]-'0':1;
bool enabled=argc<3; g_stream_requested.store(enabled);
memset(pixels,0x5a,sizeof pixels); gIonMem.vir=(uintptr_t)pixels;
if(!g_encoder_frames.init(sizeof pixels)) return 2;
g_encoder_frames.enable(enabled);
released=release_gate.get_future().share();
auto entered_future=entered.get_future();
std::promise<bool> received; auto ready=received.get_future();
std::thread consumer([&] { LatestFrame::Lease lease; bool ok=g_encoder_frames.take(lease);
 if(ok) { ok=lease.id==17 && lease.pts==123 && lease.monotonic_us==456 && !memcmp(lease.data,pixels,sizeof pixels); g_encoder_frames.release(lease); }
 received.set_value(ok); });
std::thread producer(dispatch);
entered_future.wait();
bool available=ready.wait_for(std::chrono::milliseconds(250))==std::future_status::ready;
release_gate.set_value(); producer.join();
if(!enabled) g_encoder_frames.stop();
bool valid=ready.get(); consumer.join(); g_encoder_frames.stop();
printf("stage=%d frame_available_while_secondary_blocked=%d frame_valid=%d\n",hold_stage,available,valid);
return enabled?(available&&valid?0:1):(!available&&!valid?0:1);
}
