#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <eosio/name.hpp>

using eosio::action_wrapper;
using eosio::asset;
using eosio::name;

/**
 * The action `powerresult` of `power.results` is a no-op.
 * It is added as an inline convenience action to `power` rental.
 * This inline convenience action does not have any effect, however,
 * its data includes the result of the parent action and appears in its trace.
 */
class [[eosio::contract("power.results")]] power_results : eosio::contract {
   public:

      using eosio::contract::contract;

      /**
       * powerresult action.
       *
       * @param fee        - rental fee amount
       * @param rented_net - amount of rented NET tokens
       * @param rented_cpu - amount of rented CPU tokens
       */
      [[eosio::action]]
      void powerresult( const asset& fee, const asset& rented_net, const asset& rented_cpu );

      using powerresult_action  = action_wrapper<"powerresult"_n,  &power_results::powerresult>;
};
