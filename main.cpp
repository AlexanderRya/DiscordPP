#include <iostream>
#include <vector>
#include <string>
#include <ctime>
#include <chrono>
#include <unordered_map>
#include <utility>
#include <algorithm>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <cpr/cpr.h>
#include <cpprest/ws_client.h>
#include <fmt/format.h>

using namespace web;
using namespace web::websockets::client;
using namespace std::literals;

using snowflake = uint64_t;

std::string get_api_url() {
	return "https://discordapp.com/api/v6";
}
auto get_ws_url(const std::string& token) {
	try {
		return nlohmann::json::parse(cpr::Get(
				cpr::Url{ get_api_url() + "/gateway/bot" },
				cpr::Header{ { "Authorization", "Bot " + token } }).text)["url"].get<std::string>();
	} catch (...) {
		std::cout << "Error, invalid token.\n\n";
		std::abort();
	}
}
struct dummy{};

void print_time(dummy) {
	auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	std::cout << std::put_time(std::localtime(&t), "[%H:%M:%S] ");
}

std::ostream& operator <<(std::ostream& os, void(*f)(dummy)) {
	f({});
	return os;
}

static const std::string user_agent = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/76.0.3809.132 Safari/537.36";

class bot {
	bool init_c = false;
	std::string name;
	std::string token;
	int discriminator;
	bool connection_closed = false;
	std::vector<std::thread> v;
	websocket_client c;
	std::unordered_map<std::string, std::function<void(const nlohmann::json&)>> event_map{};
	struct user {
		snowflake id;
		std::string username;
		std::string discriminator;
		std::string avatar;
	};
	struct guild {
		snowflake id;
		std::string name;
	};
	struct channel {
		snowflake id;
		std::string name;
	};
	std::vector<std::pair<guild, std::vector<channel>>> gcv;
	std::string session_id;
	char prefix;
	int seq_number = 0;
public:
	bot(const char prefix, std::string token)
		: prefix(prefix),
		  token(std::move(token)) {
		c.connect(U(get_ws_url(this->token) + "/?v=6&encoding=json"));
	}
	nlohmann::json get_user_from_id(const snowflake id) {
		auto r = cpr::Get(cpr::Url{ get_api_url() + "/users/" + std::to_string(id) },
		                  cpr::Header{ { "User-Agent", user_agent } },
		                  cpr::Header{ { "Authorization", "Bot " + token } });
		return
			r.status_code == 200
				? nlohmann::json::parse(r.text)
				: (static_cast<void>(
					std::cout << print_time 
							<< "Error code: " << r.status_code << "\nWith body: " << r.text << "\n\n"),
					nlohmann::json({}));
	}
	nlohmann::json get_guild_from_id(const snowflake id) {
		auto r = cpr::Get(cpr::Url{ get_api_url() + "/guilds/" + std::to_string(id) },
		                  cpr::Header{ { "User-Agent", user_agent } },
		                  cpr::Header{ { "Authorization", "Bot " + token } });
		return
				r.status_code == 200
				? nlohmann::json::parse(r.text)
				: (static_cast<void>(
					std::cout << print_time
							<<  "Error code: " << r.status_code << "\nWith body: " << r.text << "\n\n"),
					nlohmann::json({}));
	}
	nlohmann::json get_channel_from_id(const snowflake id) {
		auto r = cpr::Get(cpr::Url{ get_api_url() + "/channels/" + std::to_string(id) },
		                  cpr::Header{ { "User-Agent", user_agent } },
		                  cpr::Header{ { "Authorization", "Bot " + token } });
		return
				r.status_code == 200
				? nlohmann::json::parse(r.text)
				: (static_cast<void>(
						std::cout << print_time
								<< "Error code: " << r.status_code << "\nWith body: " << r.text << "\n\n"),
						nlohmann::json({}));
	}

	nlohmann::json get_guild_member_from_id(const snowflake guild_id, const snowflake member_id) {
		auto r = cpr::Get(cpr::Url{ get_api_url()
							  + "/guilds/" + std::to_string(guild_id)
							  + "/members/" + std::to_string(member_id) },
		                  cpr::Header{ { "User-Agent", user_agent } },
		                  cpr::Header{ { "Authorization", "Bot " + token } });
		return
				r.status_code == 200
				? nlohmann::json::parse(r.text)
				: (static_cast<void>(
						std::cout << print_time 
								<<  "Error code: " << r.status_code << "\nWith body: " << r.text << "\n\n"),
						nlohmann::json({}));
	}

	void ready_event_f(const nlohmann::json& j) {
		session_id = j["d"]["session_id"].get<std::string>();
		name = j["d"]["user"]["username"].get<std::string>();
		discriminator = std::stoi(j["d"]["user"]["discriminator"].get<std::string>());
		std::cout << print_time << fmt::format("READY\nLogged in as: {}#{}\n\n", name, discriminator);
	}
	void guild_create_f(const nlohmann::json& j) {
		std::vector<channel> temp;
		for (const auto& each : j["d"]["channels"]) {
			temp.push_back({
				std::stoull(each["id"].get<std::string>()),
				each["name"].get<std::string>()
			});
		}
		gcv.emplace_back(guild {
				std::stoull(j["d"]["id"].get<std::string>()),
				j["d"]["name"].get<std::string>()
			}, std::vector<channel> {
				std::move(temp)
			});
		std::cout << print_time 
				<< j["t"].get<std::string>()
		        << "\nGUILD_NAME - GUILD_ID: "
		        << gcv.back().first.name << " - " << gcv.back().first.id << "\n\n";
	}

	template <typename Fn, typename ...Args>
	void push_thread(Fn&& fn, Args&& ...args) {
		v.emplace_back(fn, args...);
	}

	void run() {
		while (!connection_closed) {
			auto req_json = nlohmann::json::parse(c.receive().get().extract_string().get());
			if (!init_c) {
				init_c = true;
				auto interval = req_json["d"]["heartbeat_interval"].get<int>();
				std::cout << "OP_CODE 10 payload received!, heartbeat interval: " << interval << "\n\n";
				push_thread([&]() {
					while (!connection_closed) {
						websocket_outgoing_message msg;
						msg.set_utf8_message(
								nlohmann::json {
										{ "op", 1 },
										{ "d", nullptr }
								}.dump());
						c.send(msg);
						std::this_thread::sleep_for(
								std::chrono::milliseconds(interval));
					}
				});
				auto jsend_op2 = nlohmann::json {
					{ "op", 2 },
					{ "d", {
						{ "token", token },
						{ "properties", {
							{ "$os", "linux" },
							{ "$browser", "chrome" },
							{ "$device", "DPP" }
						} }
					} }
				};
				websocket_outgoing_message m;
				m.set_utf8_message(jsend_op2.dump());
				c.send(m);
			} else {
				if (req_json.contains("t") && !req_json["t"].is_null()) {
					auto tpayload = req_json["t"].get<std::string>();
					if (tpayload == "READY") {
						ready_event_f(req_json);
					} else if (tpayload == "GUILD_CREATE") {
						guild_create_f(req_json);
					} else {
						if (event_map.find(tpayload) != event_map.end()) {
							event_map[tpayload](req_json);
						} else {
							std::cout << print_time << req_json["t"].get<std::string>() << " event (unhandled)\n\n";
						}
					}
					seq_number = req_json["s"].get<int>();
				} else {
					if (req_json["op"].get<int>() == 11) {
						std::cout << print_time << "Acked! with json: " << req_json.dump() << "\n\n";
					}
				}
			}
		}
		for(auto& each : v) {
			if (each.joinable()) {
				each.join();
			}
		}
	}

	void push_handler(const std::string& event_name, std::function<void(const nlohmann::json&)> f) {
		event_map[event_name] = std::move(f);
	}
};

int main() {
	websocket_callback_client c;
	std::string token;
	std::ifstream f("token.txt");
	if (!f.is_open()) {
		std::cout << "ERROR, couldn't open file.\n\n";
		return -1;
	}
	std::getline(f, token);
	bot b('>', token);
	b.run();
	return 0;
}
