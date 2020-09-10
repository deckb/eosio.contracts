#include <Runtime/Runtime.h>
#include <boost/test/unit_test.hpp>
#include <cstdlib>
#include <eosio/chain/contract_table_objects.hpp>
#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/global_property_object.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/wast_to_wasm.hpp>
#include <fc/log/logger.hpp>
#include <iostream>
#include <sstream>
#include <fstream>
#include <random>
#include <time.h>

#include "eosio.system_tester.hpp"
#include "csvwriter.hpp"
#include "csv.h"

#define PICOJSON_USE_INT64

#include "picojson.hpp"

#define GENERATE_CSV true
#define CSV_FILENAME "model_tests.csv"
#define CFG_FILENAME "_model_config.json"
#define INP_FILENAME "_rentbw_input.csv"

inline constexpr int64_t rentbw_frac = 1'000'000'000'000'000ll; // 1.0 = 10^15
inline constexpr int64_t stake_weight = 100'000'000'0000ll;     // 10^12

inline constexpr int64_t endstate_weight_ratio = 1'000'000'000'000'0ll; // 0.01 = 10^13

struct rentbw_config_resource
{
   fc::optional<int64_t> current_weight_ratio = {};
   fc::optional<int64_t> target_weight_ratio = {};
   fc::optional<int64_t> assumed_stake_weight = {};
   fc::optional<time_point_sec> target_timestamp = {};
   fc::optional<double> exponent = {};
   fc::optional<uint32_t> decay_secs = {};
   fc::optional<asset> min_price = {};
   fc::optional<asset> max_price = {};
};
FC_REFLECT(rentbw_config_resource,                                                             //
           (current_weight_ratio)(target_weight_ratio)(assumed_stake_weight)(target_timestamp) //
           (exponent)(decay_secs)(min_price)(max_price))

struct rentbw_config
{
   rentbw_config_resource net = {};
   rentbw_config_resource cpu = {};
   fc::optional<uint32_t> rent_days = {};
   fc::optional<asset> min_rent_fee = {};
};
FC_REFLECT(rentbw_config, (net)(cpu)(rent_days)(min_rent_fee))

struct rentbw_state_resource
{
   uint8_t version;
   int64_t weight;
   int64_t weight_ratio;
   int64_t assumed_stake_weight;
   int64_t initial_weight_ratio;
   int64_t target_weight_ratio;
   time_point_sec initial_timestamp;
   time_point_sec target_timestamp;
   double exponent;
   uint32_t decay_secs;
   asset min_price;
   asset max_price;
   int64_t utilization;
   int64_t adjusted_utilization;
   time_point_sec utilization_timestamp;
   int64_t fee;
};
FC_REFLECT(rentbw_state_resource,                                                                           //
           (version)(weight)(weight_ratio)(assumed_stake_weight)(initial_weight_ratio)(target_weight_ratio) //
           (initial_timestamp)(target_timestamp)(exponent)(decay_secs)(min_price)(max_price)(utilization)   //
           (adjusted_utilization)(utilization_timestamp)(fee))

struct rentbw_state
{
   uint8_t version;
   rentbw_state_resource net;
   rentbw_state_resource cpu;
   uint32_t rent_days;
   asset min_rent_fee;
};
FC_REFLECT(rentbw_state, (version)(net)(cpu)(rent_days)(min_rent_fee))

using namespace eosio_system;

struct rentbw_tester : eosio_system_tester
{
   CSVWriter csv;

   rentbw_tester()
   {
      create_accounts_with_resources({N(eosio.reserv)});

      if (GENERATE_CSV)
      {
         CSVWriter header;
         header.newRow() << "last_block_time"
                         << "before_state.net.assumed_stake_weight"
                         << "before_state.net.weight_ratio"
                         << "before_state.net.weight"
                         << "before_reserve.net"
                         << "after_reserve.net"
                         << "before_reserve.cpu"
                         << "after_reserve.cpu"
                        //  << "before_receiver.net"
                        //  << "after_receiver.net"
                         << "net.frac"
                         << "net.delta"
                        //  << "before_receiver.cpu"
                        //  << "after_receiver.cpu"
                         << "cpu.frac"
                         << "cpu.delta"
                         << "fee" //"before_payer.liquid-after_payer.liquid"
                         << "net.fee"
                         << "net.weight"
                         << "net.weight_ratio"
                         << "net.assumed_stake_weight"
                         << "net.initial_weight_ratio"
                         << "net.target_weight_ratio"
                         << "net.initial_timestamp"
                         << "net.target_timestamp"
                         << "net.exponent"
                         << "net.decay_secs"
                         << "net.min_price"
                         << "net.max_price"
                         << "net.utilization"
                         << "net.adjusted_utilization"
                         << "net.utilization_timestamp"
                         << "cpu.fee"
                         << "cpu.weight"
                         << "cpu.weight_ratio"
                         << "cpu.assumed_stake_weight"
                         << "cpu.initial_weight_ratio"
                         << "cpu.target_weight_ratio"
                         << "cpu.initial_timestamp"
                         << "cpu.target_timestamp"
                         << "cpu.exponent"
                         << "cpu.decay_secs"
                         << "cpu.min_price"
                         << "cpu.max_price"
                         << "cpu.utilization"
                         << "cpu.adjusted_utilization"
                         << "cpu.utilization_timestamp";

         header.writeToFile(CSV_FILENAME);
      }
   }

   ~rentbw_tester()
   {
      if (GENERATE_CSV)
      {
         csv.writeToFile(CSV_FILENAME, true);
      }
   }

   void start_rex()
   {
      create_account_with_resources(N(rexholder111), config::system_account_name, core_sym::from_string("1.0000"),
                                    false);
      transfer(config::system_account_name, N(rexholder111), core_sym::from_string("1001.0000"));
      BOOST_REQUIRE_EQUAL("", stake(N(rexholder111), N(rexholder111), core_sym::from_string("500.0000"),
                                    core_sym::from_string("500.0000")));
      create_account_with_resources(N(proxyaccount), config::system_account_name, core_sym::from_string("1.0000"),
                                    false, core_sym::from_string("500.0000"), core_sym::from_string("500.0000"));
      BOOST_REQUIRE_EQUAL("",
                          push_action(N(proxyaccount), N(regproxy), mvo()("proxy", "proxyaccount")("isproxy", true)));
      BOOST_REQUIRE_EQUAL("", vote(N(rexholder111), {}, N(proxyaccount)));
      BOOST_REQUIRE_EQUAL("", push_action(N(rexholder111), N(deposit),
                                          mvo()("owner", "rexholder111")("amount", asset::from_string("1.0000 TST"))));
      BOOST_REQUIRE_EQUAL("", push_action(N(rexholder111), N(buyrex),
                                          mvo()("from", "rexholder111")("amount", asset::from_string("1.0000 TST"))));
   }

   template <typename F>
   rentbw_config make_config(F f)
   {
      rentbw_config config;

      config.net.current_weight_ratio = rentbw_frac;
      config.net.target_weight_ratio = rentbw_frac / 100;
      config.net.assumed_stake_weight = stake_weight;
      config.net.target_timestamp = control->head_block_time() + fc::days(100);
      config.net.exponent = 2;
      config.net.decay_secs = fc::days(1).to_seconds();
      config.net.min_price = asset::from_string("0.0000 TST");
      config.net.max_price = asset::from_string("1000000.0000 TST");

      config.cpu.current_weight_ratio = rentbw_frac;
      config.cpu.target_weight_ratio = rentbw_frac / 100;
      config.cpu.assumed_stake_weight = stake_weight;
      config.cpu.target_timestamp = control->head_block_time() + fc::days(100);
      config.cpu.exponent = 2;
      config.cpu.decay_secs = fc::days(1).to_seconds();
      config.cpu.min_price = asset::from_string("0.0000 TST");
      config.cpu.max_price = asset::from_string("1000000.0000 TST");

      config.rent_days = 30;
      config.min_rent_fee = asset::from_string("1.0000 TST");

      f(config);
      return config;
   }

   template <typename F>
   rentbw_config make_config_from_file(const string &fname, F g)
   {
      rentbw_config config;

      stringstream ss;
      ifstream f;
      unsigned int i;

      // Read Json file
      f.open(fname, ios::binary);
      if (!f.is_open())
      {
         ilog("Unable to find model configuration file, using default");
         return make_config(g);
      }
      ss << f.rdbuf();
      f.close();

      // Parse Json data
      picojson::value v;
      ss >> v;
      string err = picojson::get_last_error();
      if (!err.empty())
      {
         cerr << err << endl;
      }

      picojson::object &o = v.get<picojson::object>()["cpu"].get<picojson::object>();

      config.net.current_weight_ratio = v.get("net").get("current_weight_ratio").get<int64_t>();
      config.net.target_weight_ratio = v.get("net").get("target_weight_ratio").get<int64_t>();
      config.net.assumed_stake_weight = v.get("net").get("assumed_stake_weight").get<int64_t>();
      config.net.target_timestamp = control->head_block_time() + fc::days(v.get("net").get("target_timestamp").get<int64_t>());
      config.net.exponent = v.get("net").get("exponent").get<int64_t>();
      config.net.decay_secs = v.get("net").get("decay_secs").get<int64_t>();
      config.net.min_price = asset::from_string(v.get("net").get("min_price").get<string>());
      config.net.max_price = asset::from_string(v.get("net").get("max_price").get<string>());

      config.cpu.current_weight_ratio = v.get("cpu").get("current_weight_ratio").get<int64_t>();
      ;
      config.cpu.target_weight_ratio = v.get("cpu").get("target_weight_ratio").get<int64_t>();
      ;
      config.cpu.assumed_stake_weight = v.get("cpu").get("assumed_stake_weight").get<int64_t>();
      config.cpu.target_timestamp = control->head_block_time() + fc::days(v.get("cpu").get("target_timestamp").get<int64_t>());
      config.cpu.exponent = v.get("cpu").get("exponent").get<int64_t>();
      config.cpu.decay_secs = v.get("cpu").get("decay_secs").get<int64_t>();
      config.cpu.min_price = asset::from_string(v.get("cpu").get("min_price").get<string>());
      config.cpu.max_price = asset::from_string(v.get("cpu").get("max_price").get<string>());

      config.rent_days = v.get("rent_days").get<int64_t>();
      config.min_rent_fee = asset::from_string(v.get("min_rent_fee").get<string>());

      g(config);
      return config;
   }

   rentbw_config make_config()
   {
      return make_config([](auto &) {});
   }

   template <typename F>
   rentbw_config make_default_config(F f)
   {
      rentbw_config config;
      f(config);
      return config;
   }

   asset calc_total_fee(const rentbw_state& state, int64_t net_frac, int64_t cpu_frac) 
   {
      auto net_util = __int128_t(net_frac) * state.net.weight / rentbw_frac;
      auto net_fee = calc_rentbw_fee(state.net, net_util);
      auto cpu_util = __int128_t(cpu_frac) * state.cpu.weight / rentbw_frac;
      auto cpu_fee = calc_rentbw_fee(state.cpu, cpu_util);
      
      ilog("net_fee: ${net_fee} cpu_fee: ${cpu_fee}",("net_fee",net_fee)("cpu_fee",cpu_fee));
      auto fee = asset(net_fee + cpu_fee, symbol{CORE_SYM});
      return fee;
   }

   asset calc_both_fees(const rentbw_state& state, int64_t net_frac, int64_t cpu_frac) 
   {
      auto net_util = __int128_t(net_frac) * state.net.weight / rentbw_frac;
      auto net_fee = calc_rentbw_fee(state.net, net_util);
      auto cpu_util = __int128_t(cpu_frac) * state.cpu.weight / rentbw_frac;
      auto cpu_fee = calc_rentbw_fee(state.cpu, cpu_util);
      
      asset fee = core_sym::from_string("0.0000");
      if (net_fee > 0 && cpu_fee > 0) {
         ilog("net_fee: ${net_fee} cpu_fee: ${cpu_fee}",("net_fee",net_fee)("cpu_fee",cpu_fee));
         fee = asset(net_fee + cpu_fee, symbol{CORE_SYM});
      }
      return fee;
   }

   int64_t calc_rentbw_fee(const rentbw_state_resource& state, int64_t utilization_increase) {
      if( utilization_increase <= 0 ) return 0;

      // Let p(u) = price as a function of the utilization fraction u which is defined for u in [0.0, 1.0].
      // Let f(u) = integral of the price function p(x) from x = 0.0 to x = u, again defined for u in [0.0, 1.0].

      // In particular we choose f(u) = min_price * u + ((max_price - min_price) / exponent) * (u ^ exponent).
      // And so p(u) = min_price + (max_price - min_price) * (u ^ (exponent - 1.0)).

      // Returns f(double(end_utilization)/state.weight) - f(double(start_utilization)/state.weight) which is equivalent to
      // the integral of p(x) from x = double(start_utilization)/state.weight to x = double(end_utilization)/state.weight.
      // @pre 0 <= start_utilization <= end_utilization <= state.weight
      auto price_integral_delta = [&state](int64_t start_utilization, int64_t end_utilization) -> double {
         double coefficient = (state.max_price.get_amount() - state.min_price.get_amount()) / state.exponent;
         double start_u     = double(start_utilization) / state.weight;
         double end_u       = double(end_utilization) / state.weight;
         return state.min_price.get_amount() * end_u - state.min_price.get_amount() * start_u +
                  coefficient * std::pow(end_u, state.exponent) - coefficient * std::pow(start_u, state.exponent);
      };

      // Returns p(double(utilization)/state.weight).
      // @pre 0 <= utilization <= state.weight
      auto price_function = [&state](int64_t utilization) -> double {
         double price = state.min_price.get_amount();
         // state.exponent >= 1.0, therefore the exponent passed into std::pow is >= 0.0.
         // Since the exponent passed into std::pow could be 0.0 and simultaneously so could double(utilization)/state.weight,
         // the safest thing to do is handle that as a special case explicitly rather than relying on std::pow to return 1.0
         // instead of triggering a domain error.
         double new_exponent = state.exponent - 1.0;
         if (new_exponent <= 0.0) {
            return state.max_price.get_amount();
         } else {
            price += (state.max_price.get_amount() - state.min_price.get_amount()) * std::pow(double(utilization) / state.weight, new_exponent);
         }

         return price;
      };

      double  fee = 0.0;
      int64_t start_utilization = state.utilization;
      int64_t end_utilization   = start_utilization + utilization_increase;

      if (start_utilization < state.adjusted_utilization) {
         fee += price_function(state.adjusted_utilization) *
                  std::min(utilization_increase, state.adjusted_utilization - start_utilization) / state.weight;
         start_utilization = state.adjusted_utilization;
      }

      if (start_utilization < end_utilization) {
         fee += price_integral_delta(start_utilization, end_utilization);
      }

      return std::ceil(fee);
   }

   action_result configbw(const rentbw_config &config)
   {
      // Verbose solution needed to work around bug in abi_serializer that fails if optional values aren't explicitly
      // specified with a null value.

      auto optional_to_variant = [](const auto &v) -> fc::variant {
         return (!v ? fc::variant() : fc::variant(*v));
      };

      auto resource_conf_vo = [&optional_to_variant](const rentbw_config_resource &c) {
         return mvo("current_weight_ratio", optional_to_variant(c.current_weight_ratio))("target_weight_ratio", optional_to_variant(c.target_weight_ratio))("assumed_stake_weight", optional_to_variant(c.assumed_stake_weight))("target_timestamp", optional_to_variant(c.target_timestamp))("exponent", optional_to_variant(c.exponent))("decay_secs", optional_to_variant(c.decay_secs))("min_price", optional_to_variant(c.min_price))("max_price", optional_to_variant(c.max_price));
      };

      auto conf = mvo("net", resource_conf_vo(config.net))("cpu", resource_conf_vo(config.cpu))("rent_days", optional_to_variant(config.rent_days))("min_rent_fee", optional_to_variant(config.min_rent_fee));

      // idump((fc::json::to_pretty_string(conf)));
      return push_action(config::system_account_name, N(configrentbw), mvo()("args", std::move(conf)));

      // If abi_serializer worked correctly, the following is all that would be needed:
      //return push_action(config::system_account_name, N(configrentbw), mvo()("args", config));
   }

   action_result rentbwexec(name user, uint16_t max)
   {
      return push_action(user, N(rentbwexec), mvo()("user", user)("max", max));
   }

   action_result rentbw(const name &payer, const name &receiver, uint32_t days, int64_t net_frac, int64_t cpu_frac,
                        const asset &max_payment)
   {
      return push_action(payer, N(rentbw),
                         mvo()("payer", payer)("receiver", receiver)("days", days)("net_frac", net_frac)(
                             "cpu_frac", cpu_frac)("max_payment", max_payment));
   }

   rentbw_state get_state()
   {
      vector<char> data = get_row_by_account(config::system_account_name, {}, N(rent.state), N(rent.state));
      return fc::raw::unpack<rentbw_state>(data);
   }

   struct account_info
   {
      int64_t ram = 0;
      int64_t net = 0;
      int64_t cpu = 0;
      asset liquid;
   };

   account_info get_account_info(account_name acc)
   {
      account_info info;
      control->get_resource_limits_manager().get_account_limits(acc, info.ram, info.net, info.cpu);
      info.liquid = get_balance(acc);
      return info;
   };

   void write_to_csv(account_info before_payer, account_info after_payer,
                     account_info before_receiver, account_info after_receiver,
                     account_info before_reserve, account_info after_reserve, 
                     rentbw_state before_state, rentbw_state after_state,
                     uint64_t net_fee, uint64_t net_frac, uint64_t cpu_fee, uint64_t cpu_frac)
   {     
      auto curr_state = get_state();
      csv.newRow()    << last_block_time()              
                      << before_state.net.assumed_stake_weight
                      << before_state.net.weight_ratio / double(rentbw_frac)
                      << before_state.net.weight
                      << float(before_reserve.net / 10000.0)
                      << float(after_reserve.net / 10000.0)
                      << float(before_reserve.cpu / 10000.0)
                      << float(after_reserve.cpu / 10000.0)
                     //  << float(before_receiver.net / 10000.0)
                     //  << float(after_receiver.net / 10000.0) 
                      << net_frac
                      << float((after_receiver.net - before_receiver.net) / 10000.0)
                     //  << (curr_state.net.utilization - before_state.net.utilization) / 10000.0
                     //  << float(before_receiver.cpu / 10000.0)
                     //  << float(after_receiver.cpu / 10000.0)
                      << cpu_frac
                      << float((after_receiver.cpu - before_receiver.cpu) / 10000.0)
                     //  << (curr_state.cpu.utilization - before_state.cpu.utilization) / 10000.0
                      << float((before_payer.liquid - after_payer.liquid).get_amount() / 10000.0) 
                      << float(net_fee / 10000.0) 
                      << curr_state.net.weight
                      << curr_state.net.weight_ratio
                      << curr_state.net.assumed_stake_weight
                      << curr_state.net.initial_weight_ratio
                      << curr_state.net.target_weight_ratio
                      << curr_state.net.initial_timestamp.sec_since_epoch()
                      << curr_state.net.target_timestamp.sec_since_epoch()
                      << curr_state.net.exponent
                      << curr_state.net.decay_secs
                      << curr_state.net.min_price.to_string()
                      << curr_state.net.max_price.to_string()
                      << curr_state.net.utilization
                      << curr_state.net.adjusted_utilization
                      << curr_state.net.utilization_timestamp.sec_since_epoch()
                      << float(cpu_fee / 10000.0) 
                      << curr_state.cpu.weight
                      << curr_state.cpu.weight_ratio
                      << curr_state.cpu.assumed_stake_weight
                      << curr_state.cpu.initial_weight_ratio
                      << curr_state.cpu.target_weight_ratio
                      << curr_state.cpu.initial_timestamp.sec_since_epoch()
                      << curr_state.cpu.target_timestamp.sec_since_epoch()
                      << curr_state.cpu.exponent
                      << curr_state.cpu.decay_secs
                      << curr_state.cpu.min_price.to_string()
                      << curr_state.cpu.max_price.to_string()
                      << curr_state.cpu.utilization
                      << curr_state.cpu.adjusted_utilization
                      << curr_state.cpu.utilization_timestamp.sec_since_epoch();
   }

   void check_rentbw(const name &payer, const name &receiver, uint32_t days, int64_t net_frac, int64_t cpu_frac,
                     const asset &expected_fee, int64_t expected_net, int64_t expected_cpu)
   {
      auto before_payer = get_account_info(payer);
      auto before_receiver = get_account_info(receiver);
      auto before_reserve = get_account_info(N(eosio.reserv));
      auto before_state = get_state();
      // fees
      auto net_util = __int128_t(net_frac) * before_state.net.weight / rentbw_frac;
      auto net_fee = calc_rentbw_fee(before_state.net, net_util);
      auto cpu_util = __int128_t(cpu_frac) * before_state.cpu.weight / rentbw_frac;
      auto cpu_fee = calc_rentbw_fee(before_state.cpu, cpu_util);
      BOOST_REQUIRE_EQUAL("", rentbw(payer, receiver, days, net_frac, cpu_frac, expected_fee));
      auto after_payer = get_account_info(payer);
      auto after_receiver = get_account_info(receiver);
      auto after_reserve = get_account_info(N(eosio.reserv));
      auto after_state = get_state();
      
      if (GENERATE_CSV)
      {

         ilog("before_state.net.assumed_stake_weight:    ${x}", ("x", before_state.net.assumed_stake_weight));
         ilog("before_state.net.weight_ratio:            ${x}",
              ("x", before_state.net.weight_ratio / double(rentbw_frac)));
         ilog("before_state.net.assumed_stake_weight:    ${x}", ("x", before_state.net.assumed_stake_weight));
         ilog("before_state.net.weight:                  ${x}", ("x", before_state.net.weight));

         ilog("before_receiver.net:                      ${x}", ("x", before_receiver.net));
         ilog("after_receiver.net:                       ${x}", ("x", after_receiver.net));
         ilog("after_receiver.net - before_receiver.net: ${x}", ("x", after_receiver.net - before_receiver.net));
         ilog("expected_net:                             ${x}", ("x", expected_net));
         ilog("before_payer.liquid - after_payer.liquid: ${x}", ("x", before_payer.liquid - after_payer.liquid));
         // ilog("fee:                                      ${x}", ("x", expected_fee));
         ilog("expected_net_fee:                         ${x}", ("x", net_fee));
         ilog("expected_cpu_fee:                         ${x}", ("x", cpu_fee));

         ilog("before_reserve.net:                       ${x}", ("x", before_reserve.net));
         ilog("after_reserve.net:                        ${x}", ("x", after_reserve.net));
         ilog("before_reserve.cpu:                       ${x}", ("x", before_reserve.cpu));
         ilog("after_reserve.cpu:                        ${x}", ("x", after_reserve.cpu));

         write_to_csv(before_payer, after_payer, 
                      before_receiver, after_receiver, 
                      before_reserve, after_reserve,
                      before_state, after_state,
                      net_fee, net_frac, cpu_fee, cpu_frac);
      }

      if (payer != receiver)
      {
         BOOST_REQUIRE_EQUAL(before_payer.ram, after_payer.ram);
         BOOST_REQUIRE_EQUAL(before_payer.net, after_payer.net);
         BOOST_REQUIRE_EQUAL(before_payer.cpu, after_payer.cpu);
         BOOST_REQUIRE_EQUAL(before_receiver.liquid, after_receiver.liquid);
      }
   }

   void produce_blocks_date(const char *str)
   {
      static bool first_timepoint = true;
      static std::chrono::system_clock::time_point cursor;
      std::tm tm = {};

      ::strptime(str, "%m/%d/%Y %H:%M:%S", &tm);
      auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
      auto time_diff = 0;

      if (first_timepoint)
      {
         cursor = tp;
         first_timepoint = false;
      }
      else
      {
         time_diff = std::chrono::duration_cast<std::chrono::milliseconds>(tp - cursor).count();
         cursor = tp;
      }

      if (time_diff > 500)
      {
         produce_block(fc::milliseconds(time_diff) - fc::milliseconds(500));
      }
   }

   void nocheck_rentbw(const name &payer, const name &receiver, uint32_t days, int64_t net_frac, int64_t cpu_frac, asset expected_fee)
   {
      auto before_payer = get_account_info(payer);
      auto before_receiver = get_account_info(receiver);
      auto before_reserve = get_account_info(N(eosio.reserv));
      auto before_state = get_state();
      // fees
      auto net_util = __int128_t(net_frac) * before_state.net.weight / rentbw_frac;
      auto net_fee = calc_rentbw_fee(before_state.net, net_util);
      auto cpu_util = __int128_t(cpu_frac) * before_state.cpu.weight / rentbw_frac;
      auto cpu_fee = calc_rentbw_fee(before_state.cpu, cpu_util);
      auto fee = calc_both_fees(before_state, net_frac, cpu_frac);

      
      rentbw(payer, receiver, days, net_frac, cpu_frac, expected_fee);
      auto after_payer = get_account_info(payer);
      auto after_receiver = get_account_info(receiver);
      auto after_reserve = get_account_info(N(eosio.reserv));
      auto after_state = get_state();
         
      if (GENERATE_CSV)
      {
            ilog("net_frac:    ${x}", ("x", net_frac));
            ilog("cpu_frac:    ${x}", ("x", cpu_frac));

            write_to_csv(before_payer, after_payer, 
                        before_receiver, after_receiver, 
                        before_reserve, after_reserve,
                        before_state, after_state,
                        net_fee, net_frac, cpu_fee, cpu_frac);
      }
   }
};

template <typename A, typename B, typename D>
bool near(A a, B b, D delta)
{
   if (abs(a - b) <= delta)
      return true;
   elog("near: ${a} ${b}", ("a", a)("b", b));
   return false;
}

BOOST_AUTO_TEST_SUITE(eosio_system_rentbw_tests)

BOOST_FIXTURE_TEST_CASE(model_tests, rentbw_tester)
try
{
   produce_block();

   BOOST_REQUIRE_EQUAL("", configbw(make_config_from_file(CFG_FILENAME, [&](auto &config) {

                       })));

   auto net_weight = stake_weight;
   auto cpu_weight = stake_weight;

   start_rex();
   create_account_with_resources(N(aaaaaaaaaaaa), config::system_account_name, core_sym::from_string("10.0000"),
                                 false, core_sym::from_string("500.0000"), core_sym::from_string("500.0000"));

   transfer(config::system_account_name, N(aaaaaaaaaaaa), core_sym::from_string("5000000.0000"));

   io::CSVReader<9> in(INP_FILENAME);
   in.read_header(io::ignore_extra_column, "datetime", "function", "payer", "receiver", "days", "net_frac", "cpu_frac", "max_payment", "queue_max");

   std::string datetime, function, payer, receiver;
   uint32_t days;
   int64_t net_frac, cpu_frac;
   std::string max_payment;
   uint16_t queue_max;

   std::chrono::system_clock::time_point cursor;
   bool first_timepoint = true;

   while (in.read_row(datetime, function, payer, receiver, days, net_frac, cpu_frac, max_payment, queue_max))
   {
      if (function == "rentbwexec")
      {
         produce_blocks_date(datetime.c_str());
         ilog("block_time: ${time}",("time",last_block_time()));
         BOOST_REQUIRE_EQUAL("", rentbwexec(config::system_account_name, queue_max));
      }
      else if (function == "rentbw")
      {
         produce_blocks_date(datetime.c_str());

         account_name payer_name = string_to_name(payer);
         account_name receiver_name = string_to_name(receiver);
         
         nocheck_rentbw(payer_name, receiver_name,
                      days, net_frac, cpu_frac, asset::from_string(max_payment + " TST"));
      }
      else
      {
         //
      }
      produce_block();
   }
}
FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(endstate_cliff_tests, rentbw_tester)
try
{
   produce_block();
   start_rex();

   BOOST_REQUIRE_EQUAL("", configbw(make_config([&](auto &config) {
                          config.net.current_weight_ratio = endstate_weight_ratio;
                          config.net.target_weight_ratio = endstate_weight_ratio;
                          config.net.assumed_stake_weight = stake_weight;
                          config.net.exponent = 4;
                          config.net.min_price = asset::from_string("500000.0000 TST");
                          config.net.max_price = asset::from_string("1000000000.0000 TST");

                          config.cpu.current_weight_ratio = endstate_weight_ratio;
                          config.cpu.target_weight_ratio = endstate_weight_ratio;
                          config.cpu.assumed_stake_weight = stake_weight;
                          config.cpu.exponent = 4;
                          config.cpu.min_price = asset::from_string("500000.0000 TST");
                          config.cpu.max_price = asset::from_string("1000000000.0000 TST");

                          config.rent_days = 30;
                          config.min_rent_fee = asset::from_string("0.0001 TST");
                       })));

   create_account_with_resources(N(aaaaaaaaaaaa), config::system_account_name, core_sym::from_string("1.0000"),
                                 false, core_sym::from_string("500.0000"), core_sym::from_string("500.0000"));

   transfer(config::system_account_name, N(aaaaaaaaaaaa), core_sym::from_string("500000000.0000"));
   
   auto curr_state = get_state();
   auto net_frac = rentbw_frac * .02;
   auto cpu_frac = rentbw_frac * .02;
   auto fee = calc_total_fee(curr_state, net_frac, cpu_frac);
   
   for (int i = 0; i < 5; i++){
      curr_state = get_state();
      fee = calc_total_fee(curr_state, net_frac, cpu_frac);
      nocheck_rentbw(N(aaaaaaaaaaaa), N(aaaaaaaaaaaa), 30, net_frac, cpu_frac, fee);
      produce_block(fc::days(1) - fc::milliseconds(500));
   }
   produce_block(fc::days(27) - fc::milliseconds(500));

   curr_state = get_state();
   fee = calc_total_fee(curr_state, net_frac, cpu_frac);
   ilog("fee: ${j}",("j",fee));
   nocheck_rentbw(N(aaaaaaaaaaaa), N(aaaaaaaaaaaa), 30, net_frac, cpu_frac, fee);
}
FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(endstate_cliff_bw_tests, rentbw_tester)
try
{
   produce_block();
   start_rex();

   BOOST_REQUIRE_EQUAL("", configbw(make_config([&](auto &config) {
                          config.net.current_weight_ratio = endstate_weight_ratio;
                          config.net.target_weight_ratio = endstate_weight_ratio;
                          config.net.assumed_stake_weight = stake_weight;
                          config.net.exponent = 4;
                          config.net.min_price = asset::from_string("500000.0000 TST");
                          config.net.max_price = asset::from_string("1000000000.0000 TST");

                          config.cpu.current_weight_ratio = endstate_weight_ratio;
                          config.cpu.target_weight_ratio = endstate_weight_ratio;
                          config.cpu.assumed_stake_weight = stake_weight;
                          config.cpu.exponent = 4;
                          config.cpu.min_price = asset::from_string("500000.0000 TST");
                          config.cpu.max_price = asset::from_string("1000000000.0000 TST");

                          config.rent_days = 30;
                          config.min_rent_fee = asset::from_string("0.0001 TST");
                       })));

   create_account_with_resources(N(aaaaaaaaaaaa), config::system_account_name, core_sym::from_string("1.0000"),
                                 false, core_sym::from_string("500.0000"), core_sym::from_string("500.0000"));


   transfer(config::system_account_name, N(aaaaaaaaaaaa), core_sym::from_string("500000000.0000"));
   auto curr_state = get_state();
   auto net_frac = rentbw_frac * .02;
   auto cpu_frac = rentbw_frac * .02;
   auto fee = calc_total_fee(curr_state, net_frac, cpu_frac);
   
   for (int i = 0; i < 5; i++){
      curr_state = get_state();
      fee = calc_total_fee(curr_state, net_frac, cpu_frac);
      nocheck_rentbw(N(aaaaaaaaaaaa), N(aaaaaaaaaaaa), 30, net_frac, cpu_frac, fee);
      produce_block(fc::days(1) - fc::milliseconds(500));
   }
   produce_block(fc::days(27) - fc::milliseconds(500));

   BOOST_REQUIRE_EQUAL("", rentbwexec(config::system_account_name, 10));

   curr_state = get_state();
   fee = calc_total_fee(curr_state, net_frac, cpu_frac);
   ilog("fee: ${j}",("j",fee));
   nocheck_rentbw(N(aaaaaaaaaaaa), N(aaaaaaaaaaaa), 30, net_frac, cpu_frac, fee);
}
FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(endstate_tests, rentbw_tester)
try
{
   produce_block();
   start_rex();

   BOOST_REQUIRE_EQUAL("", configbw(make_config([&](auto &config) {
                          config.net.current_weight_ratio = endstate_weight_ratio;
                          config.net.target_weight_ratio = endstate_weight_ratio;
                          config.net.assumed_stake_weight = stake_weight;
                          config.net.exponent = 4;
                          config.net.min_price = asset::from_string("500000.0000 TST");
                          config.net.max_price = asset::from_string("1000000000.0000 TST");

                          config.cpu.current_weight_ratio = endstate_weight_ratio;
                          config.cpu.target_weight_ratio = endstate_weight_ratio;
                          config.cpu.assumed_stake_weight = stake_weight;
                          config.cpu.exponent = 4;
                          config.cpu.min_price = asset::from_string("500000.0000 TST");
                          config.cpu.max_price = asset::from_string("1000000000.0000 TST");

                          config.rent_days = 30;
                          config.min_rent_fee = asset::from_string("0.0001 TST");
                       })));

   create_account_with_resources(N(aaaaaaaaaaaa), config::system_account_name, core_sym::from_string("1.0000"),
                                 false, core_sym::from_string("500.0000"), core_sym::from_string("500.0000"));

   transfer(config::system_account_name, N(aaaaaaaaaaaa), core_sym::from_string("500000000.0000"));
   auto curr_state = get_state();

   // produce_block(fc::days(60) - fc::milliseconds(500));
   BOOST_REQUIRE_EQUAL("", rentbwexec(config::system_account_name, 10));

   // 2%, 2%
   for (int m = 0; m < 1; m++)
   {
      // rent every day during 15 days
      for (int j = 0; j < 15; j++)
      {  
         ilog("iteration: ${j}",("j",j));
         curr_state = get_state();
         auto net_frac = rentbw_frac * .02;
         auto cpu_frac = rentbw_frac * .02;
         auto fee = calc_total_fee(curr_state, net_frac, cpu_frac);
         // idump((fc::json::to_pretty_string(curr_state)));
         // ilog("fee: ${j}",("j",fee));
         check_rentbw(N(aaaaaaaaaaaa), N(aaaaaaaaaaaa), 30, net_frac, cpu_frac,
                        fee, curr_state.net.weight * .02, curr_state.cpu.weight * .02);
         produce_block(fc::days(1) - fc::milliseconds(500));
      }
      produce_block(fc::days(30) - fc::milliseconds(500));
      BOOST_REQUIRE_EQUAL("", rentbwexec(config::system_account_name, 100));
   }   
}
FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(endstate_net_tests, rentbw_tester)
try
{
   produce_block();
   start_rex();

   BOOST_REQUIRE_EQUAL("", configbw(make_config([&](auto &config) {
                          config.net.current_weight_ratio = endstate_weight_ratio;
                          config.net.target_weight_ratio = endstate_weight_ratio;
                          config.net.assumed_stake_weight = stake_weight;
                          config.net.exponent = 4;
                          config.net.min_price = asset::from_string("500000.0000 TST");
                          config.net.max_price = asset::from_string("1000000000.0000 TST");

                          config.cpu.current_weight_ratio = endstate_weight_ratio;
                          config.cpu.target_weight_ratio = endstate_weight_ratio;
                          config.cpu.assumed_stake_weight = stake_weight;
                          config.cpu.exponent = 4;
                          config.cpu.min_price = asset::from_string("500000.0000 TST");
                          config.cpu.max_price = asset::from_string("1000000000.0000 TST");

                          config.rent_days = 30;
                          config.min_rent_fee = asset::from_string("0.0001 TST");
                       })));

   create_account_with_resources(N(aaaaaaaaaaaa), config::system_account_name, core_sym::from_string("1.0000"),
                                 false, core_sym::from_string("500.0000"), core_sym::from_string("500.0000"));

   transfer(config::system_account_name, N(aaaaaaaaaaaa), core_sym::from_string("500000000.0000"));
   auto curr_state = get_state();

   // produce_block(fc::days(60) - fc::milliseconds(500));
   BOOST_REQUIRE_EQUAL("", rentbwexec(config::system_account_name, 10));

   // 2%, 2%
   for (int m = 0; m < 1; m++)
   {
      // rent every day during 15 days
      for (int j = 0; j < 15; j++)
      {  
         ilog("iteration: ${j}",("j",j));
         curr_state = get_state();
         auto net_frac = rentbw_frac * .02;
         auto cpu_frac = rentbw_frac * .02;
         auto fee = calc_total_fee(curr_state, net_frac, cpu_frac);
         // idump((fc::json::to_pretty_string(curr_state)));
         // ilog("fee: ${j}",("j",fee));
         check_rentbw(N(aaaaaaaaaaaa), N(aaaaaaaaaaaa), 30, net_frac, 0,
                        fee, curr_state.net.weight * .02, 0);
         produce_block(fc::days(1) - fc::milliseconds(500));
      }
      produce_block(fc::days(30) - fc::milliseconds(500));
      BOOST_REQUIRE_EQUAL("", rentbwexec(config::system_account_name, 100));
   } 
}
FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(endstate_year_tests, rentbw_tester)
try
{
   produce_block();
   start_rex();

   BOOST_REQUIRE_EQUAL("", configbw(make_config([&](auto &config) {
                          config.net.current_weight_ratio = endstate_weight_ratio;
                          config.net.target_weight_ratio = endstate_weight_ratio;
                          config.net.assumed_stake_weight = stake_weight;
                          config.net.exponent = 4;
                          config.net.min_price = asset::from_string("500000.0000 TST");
                          config.net.max_price = asset::from_string("1000000000.0000 TST");

                          config.cpu.current_weight_ratio = endstate_weight_ratio;
                          config.cpu.target_weight_ratio = endstate_weight_ratio;
                          config.cpu.assumed_stake_weight = stake_weight;
                          config.cpu.exponent = 4;
                          config.cpu.min_price = asset::from_string("500000.0000 TST");
                          config.cpu.max_price = asset::from_string("1000000000.0000 TST");

                          config.rent_days = 30;
                          config.min_rent_fee = asset::from_string("0.0001 TST");
                       })));

   auto net_weight = stake_weight;
   auto cpu_weight = stake_weight;

   create_account_with_resources(N(aaaaaaaaaaaa), config::system_account_name, core_sym::from_string("100.0000"),
                                 false, core_sym::from_string("10000.0000"), core_sym::from_string("10000.0000"));

   transfer(config::system_account_name, N(aaaaaaaaaaaa), core_sym::from_string("500000000.0000"));
   auto curr_state = get_state();

   // produce_block(fc::days(60) - fc::milliseconds(500));
   BOOST_REQUIRE_EQUAL("", rentbwexec(config::system_account_name, 10));

   // generate random number between 9,900,000,000 (0.01%) and 49,500,000,000 (0.05%)
   std::default_random_engine generator;
   std::uniform_int_distribution<uint64_t> distribution(990'000'0000, 4'950'000'0000);
   // rent 15 times a day for a year
   for (int m = 0; m < 366; m++)
   {
      // rent once a day for a year
      for (int j = 0; j <= 15; j++)
      {  
         // ilog("day: ${j}",("j",j));
         curr_state = get_state();
         auto frac = distribution(generator);
         auto net_frac = frac * .25;
         auto cpu_frac = frac * .75;
         auto fee = calc_total_fee(curr_state, net_frac, cpu_frac);
         nocheck_rentbw(N(aaaaaaaaaaaa), N(aaaaaaaaaaaa), 30, net_frac, cpu_frac, fee);
         produce_block(fc::hours(1) - fc::milliseconds(500));
      }
      produce_block(fc::days(1) - fc::milliseconds(500));
      // produce_block(fc::days(30) - fc::milliseconds(500));
      // BOOST_REQUIRE_EQUAL("", rentbwexec(config::system_account_name, 100));
   }   
}
FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(transition_year_test, rentbw_tester)
try
{
   produce_block();
   start_rex();

   BOOST_REQUIRE_EQUAL("", configbw(make_config_from_file(CFG_FILENAME, [&](auto &config) {
                       })));
   // BOOST_REQUIRE_EQUAL("", configbw(make_config([&](auto &config) {
   //                        config.net.current_weight_ratio = rentbw_frac; // 10^15
   //                        config.net.target_weight_ratio = rentbw_frac / 100; // 10^13
   //                        config.net.assumed_stake_weight = 131'668'749;

   //                        config.net.exponent = 4;
   //                        config.net.min_price = asset::from_string("500000.0000 TST");
   //                        config.net.max_price = asset::from_string("1000000000.0000 TST");
   //                        config.net.decay_secs = 86400;
   //                        config.net.target_timestamp = control->head_block_time() + fc::days(365);

   //                        config.cpu.current_weight_ratio = rentbw_frac;
   //                        config.cpu.target_weight_ratio = rentbw_frac / 100;
   //                        config.cpu.assumed_stake_weight = 395'006'248; 
   //                        // config.cpu.assumed_stake_weight = 3'776'305'228; // jungle

   //                        config.cpu.exponent = 4;
   //                        config.cpu.min_price = asset::from_string("500000.0000 TST");
   //                        config.cpu.max_price = asset::from_string("1000000000.0000 TST");
   //                        config.cpu.decay_secs = 86400;
   //                        config.cpu.target_timestamp = control->head_block_time() + fc::days(365);

   //                        config.rent_days = 30;
   //                        config.min_rent_fee = asset::from_string("0.0001 TST");
   //                     })));

   auto curr_state = get_state();
   idump((fc::json::to_pretty_string(curr_state)));
   auto net_weight = stake_weight;
   auto cpu_weight = stake_weight;

   create_account_with_resources(N(aaaaaaaaaaaa), config::system_account_name, core_sym::from_string("100.0000"),
                                 false, core_sym::from_string("10000.0000"), core_sym::from_string("10000.0000"));

   auto acct_bal = core_sym::from_string("500000000.0000");
   transfer(config::system_account_name, N(aaaaaaaaaaaa), acct_bal);
   produce_block();

   // skip first 60 days
   // produce_block(fc::days(60));

   // generate random number between 0.01% and 0.05%
   std::default_random_engine generator;
   std::uniform_int_distribution<uint64_t> distribution(rentbw_frac * 0.0001, rentbw_frac * 0.001);
   // rent 15 times a day for a year
   for (int m = 0; m < 365; m++)
   {
      // BOOST_REQUIRE_EQUAL("", rentbwexec(config::system_account_name, 100));
      // rent once a day for a year
      for (int j = 0; j <= 15; j++)
      {  
         BOOST_REQUIRE_EQUAL("", rentbwexec(config::system_account_name, 100));
         // ilog("day: ${j}",("j",j));
         curr_state = get_state();
         auto frac = distribution(generator);
         // auto net_frac = frac * .25;
         // auto cpu_frac = frac * .75;
         auto fee = calc_both_fees(curr_state, frac, frac);
         if(fee > core_sym::from_string("0.0000")){
            nocheck_rentbw(N(aaaaaaaaaaaa), N(aaaaaaaaaaaa), 30, frac, frac, acct_bal);
         }
         produce_block(fc::hours(1) - fc::milliseconds(500));
      }
      produce_block(fc::days(1) - fc::milliseconds(500));
      // produce_block(fc::days(30) - fc::milliseconds(500));
   }   
}
FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(transition_year_lower_fee_test, rentbw_tester)
try
{
   produce_block();
   start_rex();

   BOOST_REQUIRE_EQUAL("", configbw(make_config([&](auto &config) {
                          config.net.current_weight_ratio = rentbw_frac; // 10^15
                          config.net.target_weight_ratio = rentbw_frac / 100; // 10^13
                          config.net.assumed_stake_weight = 131'668'749;
                          // config.net.assumed_stake_weight = 944'076'307; // jungle

                          config.net.exponent = 2;
                          config.net.min_price = asset::from_string("0.0000 TST");
                          config.net.max_price = asset::from_string("10000000.0000 TST");
                          config.net.decay_secs = 86400;
                          config.net.target_timestamp = control->head_block_time() + fc::days(365);

                          config.cpu.current_weight_ratio = rentbw_frac;
                          config.cpu.target_weight_ratio = rentbw_frac / 100;
                          config.cpu.assumed_stake_weight = 395'006'248; 
                          // config.cpu.assumed_stake_weight = 3'776'305'228; // jungle

                          config.cpu.exponent = 2;
                          config.cpu.min_price = asset::from_string("0.0000 TST");
                          config.cpu.max_price = asset::from_string("10000000.0000 TST");
                          config.cpu.decay_secs = 86400;
                          config.cpu.target_timestamp = control->head_block_time() + fc::days(365);

                          config.rent_days = 30;
                          config.min_rent_fee = asset::from_string("0.0001 TST");
                       })));
   auto curr_state = get_state();
   idump((fc::json::to_pretty_string(curr_state)));
   auto net_weight = stake_weight;
   auto cpu_weight = stake_weight;

   create_account_with_resources(N(aaaaaaaaaaaa), config::system_account_name, core_sym::from_string("100.0000"),
                                 false, core_sym::from_string("10000.0000"), core_sym::from_string("10000.0000"));

   auto acct_bal = core_sym::from_string("500000000.0000");
   transfer(config::system_account_name, N(aaaaaaaaaaaa), acct_bal);
   produce_block();

   // skip first 60 days
   // produce_block(fc::days(60));

   // generate random number between 0.01% and 0.05%
   std::default_random_engine generator;
   std::uniform_int_distribution<uint64_t> distribution(rentbw_frac * 0.0001, rentbw_frac * 0.0005);
   // rent 15 times a day for a year
   for (int m = 0; m < 365; m++)
   {
      // BOOST_REQUIRE_EQUAL("", rentbwexec(config::system_account_name, 100));
      // rent once a day for a year
      for (int j = 0; j <= 15; j++)
      {  
         BOOST_REQUIRE_EQUAL("", rentbwexec(config::system_account_name, 100));
         // ilog("day: ${j}",("j",j));
         curr_state = get_state();
         auto frac = distribution(generator);
         // auto net_frac = frac * .25;
         // auto cpu_frac = frac * .75;
         auto fee = calc_both_fees(curr_state, frac, frac);
         if(fee > core_sym::from_string("0.0000")){
            nocheck_rentbw(N(aaaaaaaaaaaa), N(aaaaaaaaaaaa), 30, frac, frac, acct_bal);
         }
         produce_block(fc::hours(1) - fc::milliseconds(500));
      }
      produce_block(fc::days(1) - fc::milliseconds(500));
      // produce_block(fc::days(30) - fc::milliseconds(500));
   }   
}
FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(jungle_transition_test, rentbw_tester)
try
{
   produce_block();
   start_rex();

   BOOST_REQUIRE_EQUAL("", configbw(make_config([&](auto &config) {
                          config.net.current_weight_ratio = rentbw_frac;
                          config.net.target_weight_ratio = rentbw_frac / 100;
                          config.net.assumed_stake_weight = 944'076'307; // jungle

                          config.net.exponent = 2;
                        //   config.net.min_price = asset::from_string("500000.0000 TST");
                        //   config.net.max_price = asset::from_string("1000000000.0000 TST");
                          config.net.min_price = asset::from_string("0.0000 TST");
                          config.net.max_price = asset::from_string("10000000.0000 TST");
                          config.net.decay_secs = 86400;
                          config.net.target_timestamp = control->head_block_time() + fc::days(12);

                          config.cpu.current_weight_ratio = rentbw_frac;
                          config.cpu.target_weight_ratio = rentbw_frac / 100;
                          config.cpu.assumed_stake_weight = 3'776'305'228; // jungle

                          config.cpu.exponent = 2;
                        //   config.cpu.min_price = asset::from_string("500000.0000 TST");
                        //   config.cpu.max_price = asset::from_string("1000000000.0000 TST");
                          config.cpu.min_price = asset::from_string("0.0000 TST");
                          config.cpu.max_price = asset::from_string("10000000.0000 TST");
                          config.cpu.decay_secs = 86400;
                          config.cpu.target_timestamp = control->head_block_time() + fc::days(12);

                          config.rent_days = 30;
                          config.min_rent_fee = asset::from_string("0.0001 TST");
                       })));
   auto curr_state = get_state();
   idump((fc::json::to_pretty_string(curr_state)));
   auto net_weight = stake_weight;
   auto cpu_weight = stake_weight;

   create_account_with_resources(N(aaaaaaaaaaaa), config::system_account_name, core_sym::from_string("100.0000"),
                                 false, core_sym::from_string("10000.0000"), core_sym::from_string("10000.0000"));

   transfer(config::system_account_name, N(aaaaaaaaaaaa), core_sym::from_string("500000000.0000"));
   produce_block();
   produce_block(fc::days(30) - fc::milliseconds(500));

   for( int i =0; i < 60; i++){
      
      produce_block(fc::days(1) - fc::milliseconds(500));
      BOOST_REQUIRE_EQUAL("", rentbwexec(config::system_account_name, 10));
      for( int j = 0; j < 1; j++)
      {
         curr_state = get_state();
         auto net_frac = long(rentbw_frac * 0.01);
         auto cpu_frac = long(rentbw_frac * 0.01);
         auto fee = calc_both_fees(curr_state, net_frac, cpu_frac);
         if(fee > core_sym::from_string("0.0000")) {
            nocheck_rentbw(N(aaaaaaaaaaaa), N(aaaaaaaaaaaa), 30, net_frac, cpu_frac, fee);
         }
      }
   }

   curr_state = get_state();
   idump((fc::json::to_pretty_string(curr_state)));
   idump((fc::json::to_pretty_string(last_block_time())));
   auto reserv_acct = get_account_info(N(eosio.reserv));
   ilog("net: ${net} cpu: ${cpu}",("net", reserv_acct.net)("cpu", reserv_acct.cpu));

   // // generate random number between 9,900,000,000 (0.01%) and 49,500,000,000 (0.05%)
   // std::default_random_engine generator;
   // std::uniform_int_distribution<uint64_t> distribution(990'000'0000, 4'950'000'0000);
   // // rent 15 times a day for a year
   // for (int m = 0; m < 366; m++)
   // {
   //    // rent once a day for a year
   //    for (int j = 0; j <= 15; j++)
   //    {  
   //       // ilog("day: ${j}",("j",j));
   //       curr_state = get_state();
   //       auto frac = distribution(generator);
   //       auto net_frac = frac * .25;
   //       auto cpu_frac = frac * .75;
   //       auto fee = calc_total_fee(curr_state, net_frac, cpu_frac);
   //       nocheck_rentbw(N(aaaaaaaaaaaa), N(aaaaaaaaaaaa), 30, net_frac, cpu_frac, fee);
   //       produce_block(fc::hours(1) - fc::milliseconds(500));
   //    }
   //    produce_block(fc::days(1) - fc::milliseconds(500));
   //    // produce_block(fc::days(30) - fc::milliseconds(500));
   //    // BOOST_REQUIRE_EQUAL("", rentbwexec(config::system_account_name, 100));
   // }   
}
FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
