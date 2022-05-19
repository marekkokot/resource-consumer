#include <iostream>
#include <CLI11.hpp>
#include <spdlog/spdlog.h>
#include <vector>
#include <thread>
#include <cinttypes>
#include <sstream>
#include <mutex>
#include <memory>
#include <random>

struct Config {
	uint32_t n_threads = 1;
	uint64_t tot_mem_to_alloc = 1ull << 30;
	uint64_t single_buffer_size = 1ull << 25;
};

struct TransformMemoryString {
	std::string operator()(std::string x) {
		if (x.empty())
			return x;		
		uint64_t mul = 1;
		if (x.back() == 'G')
			mul = 1'000'000'000;
		else if (x.back() == 'M')
			mul = 1'000'000;
		else if (x.back() == 'M')
			mul = 1'000;

		if (mul != 1)
			x.pop_back();
		uint64_t num;

		std::istringstream iss(x);
		iss >> num;
		num *= mul;
		return std::to_string(num);
	}
};

int main(int argc, char** argv) {
	CLI::App app{ "Resource consumer" };	
	Config config;
	app.add_option("-t,--threads", config.n_threads, "Number of threads");
	app.add_option("-m,--mem-to-alloc", config.tot_mem_to_alloc, "Total memory to allocate")->transform(TransformMemoryString{});
	app.add_option("-b,--buff-size", config.single_buffer_size, "Size of single allocation")->transform(TransformMemoryString{});
	
	CLI11_PARSE(app, argc, argv);
	
	std::mutex mtx;
	uint64_t cur_alocated=0;
	auto next_task = [&]() {
		std::lock_guard lck(mtx);
		auto to_alloc = std::min(config.tot_mem_to_alloc - cur_alocated, config.single_buffer_size);
		cur_alocated += to_alloc;
		spdlog::trace("Allocated {}/{}", cur_alocated, config.tot_mem_to_alloc);
		return to_alloc;
	};

	std::vector<std::thread> threads;
	std::vector<std::vector<uint8_t>> allocated_memory;
	spdlog::info("Starting allocations of {} B with {} threads", config.tot_mem_to_alloc, config.n_threads);
	for (uint32_t tno = 0; tno < config.n_threads; ++tno) 
		threads.emplace_back([&]() {
			static std::mutex mtx;
			uint64_t to_aloc;
			while (to_aloc = next_task()) {
				spdlog::info("Allocating {} B", to_aloc);
				std::vector<uint8_t> v(to_aloc);
				std::lock_guard lck(mtx);
				allocated_memory.emplace_back(std::move(v));
			}			
		});
	
	for (auto& t : threads)
		t.join();

	spdlog::info("All allocations done");

	
	std::default_random_engine eng;
	std::vector<uint32_t> chunds_id_base(allocated_memory.size());
	std::iota(chunds_id_base.begin(), chunds_id_base.end(), 0);
	std::condition_variable cv;
	auto select_chunk = [&]() {
		std::unique_lock lck(mtx);
		cv.wait(lck, [&]() {return !chunds_id_base.empty(); });
		std::uniform_int_distribution<uint32_t> dist(0, chunds_id_base.size() - 1);
		auto x = dist(eng);
		std::swap(chunds_id_base.back(), chunds_id_base[x]);
		auto res = chunds_id_base.back();
		chunds_id_base.pop_back();
		return res;
	};

	auto return_chunk = [&](uint32_t chunk_id) {
		std::lock_guard lck(mtx);
		chunds_id_base.push_back(chunk_id);
		cv.notify_one();
	};

	for (auto& x : threads)
		x = std::thread([&]() {
			std::uniform_int_distribution<int> dist(0, 9);
			std::default_random_engine eng(std::random_device{}());
			uint64_t my_tot{};
			while (true) {
				auto id = select_chunk();
				bool write = dist(eng) == 0;
				if (write) {
					spdlog::info("writing random values to chunk {}", id);
					for (auto& x : allocated_memory[id])
						x = dist(eng);
				}		
				else {
					spdlog::info("reading values from chunk {}", id);
					for (auto& x : allocated_memory[id])
						my_tot += x;
				}
				return_chunk(id);
			}
		});

	for (auto& t : threads)
		t.join();
}