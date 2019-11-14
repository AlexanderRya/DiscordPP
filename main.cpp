#include "plib.hpp"
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

void print_time() {
	auto t = std::chrono::system_clock::to_time_t(std::chrono::high_resolution_clock::now());
	auto ft = *std::localtime(&t);
	std::cout << fmt::format("[{}:{}:{}] ", ft.tm_hour, ft.tm_min, ft.tm_sec);
}

std::ostream& operator <<(std::ostream& os, void(*f)()) {
	f();
	return os;
}

static const std::string user_agent = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/76.0.3809.132 Safari/537.36";

enum PERMS : std::size_t {
	KICK_MEMBERS = 0x00000002,
	BAN_MEMBERS = 0x00000004,
	ADMIN = 0x00000008
};

class bot {
	std::string name;
	std::string token;
	int discriminator;
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
public:
	auto get_token() { return token; }
	bot(const char prefix, std::string token) : prefix(prefix), token(std::move(token)) {}
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
	void message_create_f(const nlohmann::json& j) {
		if (j["d"]["author"]["username"].get<std::string>() == name) return;
		auto content = j["d"]["content"].get<std::string>();
		if (content[0] == prefix) {
			on_command(std::stoull(j["d"]["author"]["id"].get<std::string>()),
					content,
					std::stoull(j["d"]["channel_id"].get<std::string>()),
					std::stoull(j["d"]["guild_id"].get<std::string>()));
			return;
		}
		auto guild_name = get_guild_from_id(
				std::stoull(j["d"]["guild_id"].get<std::string>()))["name"].get<std::string>();
		auto channel_name = get_channel_from_id(
				std::stoull(j["d"]["channel_id"].get<std::string>()))["name"].get<std::string>();
		std::cout <<
			print_time << 
			fmt::format("MESSAGE_CREATE callback\nGuild name: {}\nChannel name: {}\nUser: {}#{}\nmessage: {}\n\n",
				guild_name,
				channel_name,
				j["d"]["author"]["username"].get<std::string>(),
				j["d"]["author"]["discriminator"].get<std::string>(),
				content);
	}
	void message_update_f(const nlohmann::json& j) {
		auto content = j["d"]["content"].get<std::string>();
		auto guild_name = get_guild_from_id(
				std::stoull(j["d"]["guild_id"].get<std::string>()))["name"].get<std::string>();
		auto channel_name = get_channel_from_id(
				std::stoull(j["d"]["channel_id"].get<std::string>()))["name"].get<std::string>();
		std::cout << 
			print_time << 
			fmt::format("MESSAGE_UPDATE callback\nGuild name: {}\nChannel name: {}\nUser: {}#{} has updated a message: {}\n\n",
		        guild_name,
		        channel_name,
		        j["d"]["author"]["username"].get<std::string>(),
		        j["d"]["author"]["discriminator"].get<std::string>(),
		        content);
	}
	void message_delete_f(const nlohmann::json&) {
		std::cout << print_time << "DELETE_MESSAGE callback\n\n";
	}

	void presence_update_f(const nlohmann::json& j) {
		/*auto user = get_user_from_id(std::stoull(j["d"]["user"]["id"].get<std::string>()));
		auto guild = get_guild_from_id(std::stoull(j["d"]["guild_id"].get<std::string>()));
		if (j["d"]["activities"].empty() || j["d"]["activities"][0]["name"].get<std::string>() == "Custom Status") {
			std::cout <<
				fmt::format("PRESENCE_UPDATE callback\nUser {}#{} in guild: {} has updated their status to: {}\n\n",
					user["username"].get<std::string>(),
					user["discriminator"].get<std::string>(),
					guild["name"].get<std::string>(),
					j["d"]["status"].get<std::string>());
		} else {
			std::cout <<
				fmt::format("PRESENCE_UPDATE callback\nUser {}#{} in guild: {} is playing: {}\n\n",
			        user["username"].get<std::string>(),
			        user["discriminator"].get<std::string>(),
			        guild["name"].get<std::string>(),
			        j["d"]["activities"][0]["name"].get<std::string>());
		}*/
		std::cout << print_time << "PRESENCE_UPDATE callback\n\n";
	}

	void on_command(const snowflake sender, const std::string& command, const snowflake channel_id, const snowflake guild_id) {
		auto body =
				nlohmann::json {
					{ "tts", false },
				};
		if (command == (prefix + "test"s)) {
			body["content"] = "test";
		} else if (command == (prefix + "mammt"s)) {
			body["content"] = "no u";
		} else if (command.find(prefix + "ban"s) != std::string::npos) {
			auto cidx = command.find_first_of('>', 1);
			if (cidx == std::string::npos) {
				send_message("Error, invalid syntax, usage: "s + prefix + "ban `@user`", channel_id);
				return;
			}
			size_t start_idx = cidx - 18;
			size_t end_idx = cidx;
			auto buser = get_guild_member_from_id(guild_id, std::stoull(std::string{ command.begin() + start_idx, command.begin() + end_idx }));
			auto user = get_guild_member_from_id(guild_id, sender);
			std::vector<std::pair<snowflake, u32>> groles;
			auto temp = get_guild_from_id(guild_id)["roles"];
			for (const auto& role : temp) {
				groles.emplace_back(
					std::stoull(role["id"].get<std::string>()),
					(role["permissions"].get<u32>()));
			}
			bool b = false;
			if (user["roles"].empty()) {
				if (((groles[0].second & PERMS::ADMIN) == PERMS::ADMIN)
				    || ((groles[0].second & PERMS::BAN_MEMBERS) == PERMS::BAN_MEMBERS)) {
					//send_message("Can ban.", channel_id);
					return;
				} else {
					send_message("Error, you don't have enough permissions to use the \">ban\" command.", channel_id);
					return;
				}
			}
			for (auto i = 0ul; i < user["roles"].size(); ++i) {
				auto role_id = std::stoull(user["roles"][i].get<std::string>());
				auto ridx = std::distance(groles.begin(), std::find_if(groles.begin(), groles.end(), [&](const auto& x) {
					return x.first == role_id;
				}));
				if (((groles[ridx].second & PERMS::ADMIN) == PERMS::ADMIN)
						|| ((groles[ridx].second & PERMS::BAN_MEMBERS) == PERMS::BAN_MEMBERS)) {
						send_message("Can ban.", channel_id);
						b = true;
						break;
				}
			}
			if (!b) {
				send_message("Error, you don't have enough permissions to use the \">ban\" command.", channel_id);
			}
			return;
		}
		if (!body.contains("content")) {
			body["content"] = "Unknown command.";
		}
		auto r = cpr::Post(cpr::Url{ get_api_url() + "/channels/" + std::to_string(channel_id) + "/messages" },
		              cpr::Header{ { "Content-Type", "application/json" } },
		              cpr::Header{ { "User-Agent", user_agent } },
		              cpr::Header{ { "Authorization", "Bot " + token } },
		              cpr::Body{ body.dump() });
		if (r.status_code == 200) {
			std::cout << print_time << "Command callback: successfully sent message\n\n";
		} else {
			std::cout << print_time << "Error: " << r.status_code << "\n" << "With body: " << r.text << "\n\n";
		}
	}

	void send_message(const std::string& msg, const snowflake channel_id) {
		auto body =
				nlohmann::json {
						{ "content", msg },
						{ "tts", false },
				}.dump();
		auto r = cpr::Post(cpr::Url{ get_api_url() + "/channels/" + std::to_string(channel_id) + "/messages" },
		                   cpr::Header{ { "Content-Type", "application/json" } },
		                   cpr::Header { { "User-Agent", user_agent } },
		                   cpr::Header{ { "Authorization", "Bot " + token } },
		                   cpr::Body{ body });
		if (r.status_code == 200) {
			std::cout << "Send message callback: successfully sent message\n\n";
		} else if (r.status_code == 429) {
			std::this_thread::sleep_for(std::chrono::milliseconds(nlohmann::json::parse(r.text)["retry_after"].get<int>()));
			send_message(msg, channel_id);
		} else {
			std::cout << "Error: " << r.status_code << "\n" << "With body: " << r.text << "\n";
		}
	}
};

int main() {
	int seq_number = 0;
	websocket_client c;
	std::string token;
	std::ifstream f("token.txt");
	if (!f.is_open()) {
		std::cout << "ERROR, couldn't open file.\n\n";
		return -1;
	}
	std::getline(f, token);
	bot b('>', token);
	std::unordered_map<std::string, void(bot::*)(const nlohmann::json&)> event_map {
		{ "READY", &bot::ready_event_f },
		{ "GUILD_CREATE", &bot::guild_create_f },
		{ "MESSAGE_CREATE", &bot::message_create_f },
		{ "MESSAGE_UPDATE", &bot::message_update_f },
		{ "MESSAGE_DELETE", &bot::message_delete_f },
		{ "PRESENCE_UPDATE", &bot::presence_update_f }
	};
	bool connection_closed = false;
	c.connect(U(get_ws_url(b.get_token()) + "/?v=6&encoding=json"));
	auto s = c.receive().get().extract_string().get();
	auto req_json = nlohmann::json::parse(s);
	auto interval = req_json["d"]["heartbeat_interval"].get<int>();
	std::cout << "OP_CODE 10 payload received!, heartbeat interval: " << interval << "\n\n";
	std::thread t([&connection_closed, &c, &interval]() {
		while (!connection_closed) {
			websocket_outgoing_message msg;
			msg.set_utf8_message(nlohmann::json {
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
			{ "token", b.get_token() },
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
	while (true) {
		auto j = nlohmann::json::parse(c.receive().get().extract_string().get());
		if (j.contains("t") && !j["t"].is_null()) {
			if (event_map.find(j["t"].get<std::string>()) != event_map.end()) {
				(b.*(event_map[j["t"].get<std::string>()]))(j);
			} else {
				std::cout << print_time << j["t"].get<std::string>() << "\n\n";
			}
			seq_number = j["s"].get<int>();
		} else {
			if (j["op"].get<int>() == 11) {
				auto t = std::chrono::system_clock::to_time_t(std::chrono::high_resolution_clock::now());
				auto ft = *std::localtime(&t);
				std::cout << print_time << "Acked! with json: " << j.dump() << "\n\n";
			}
		}
		std::this_thread::sleep_for(10ms);
	}
	return 0;
}
