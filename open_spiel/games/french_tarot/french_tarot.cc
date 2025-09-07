// Copyright 2019 DeepMind Technologies Limited
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "open_spiel/games/french_tarot/french_tarot.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>

#include "open_spiel/abseil-cpp/absl/strings/str_cat.h"
#include "open_spiel/game_parameters.h"
#include "open_spiel/observer.h"
#include "open_spiel/policy.h"
#include "open_spiel/spiel.h"
#include "open_spiel/spiel_utils.h"
#include "french_tarot.h"

namespace open_spiel
{
  namespace french_tarot
  {
    namespace
    {

      // Default parameters.
      constexpr int kDefaultPlayers = 4;

      // Facts about the game
      const GameType kGameType{
          /*short_name=*/"french_tarot",
          /*long_name=*/"French Tarot",
          GameType::Dynamics::kSequential,
          GameType::ChanceMode::kExplicitStochastic,
          GameType::Information::kImperfectInformation,
          GameType::Utility::kZeroSum,
          GameType::RewardModel::kTerminal,
          /*max_num_players=*/4,
          /*min_num_players=*/3,
          /*provides_information_state_string=*/true,
          /*provides_information_state_tensor=*/true,
          /*provides_observation_string=*/true,
          /*provides_observation_tensor=*/true,
          /*parameter_specification=*/
          {{"players", GameParameter(kDefaultPlayers)}},
          /*default_loadable=*/true,
          /*provides_factored_observation_string=*/true,
      };

      std::shared_ptr<const Game> Factory(const GameParameters &params)
      {
        return std::shared_ptr<const Game>(new FrenchTarotGame(params));
      }

      REGISTER_SPIEL_GAME(kGameType, Factory);

      open_spiel::RegisterSingleTensorObserver single_tensor(kGameType.short_name);
    } // namespace

    class FrenchTarotObserver : public Observer
    {
    public:
      FrenchTarotObserver(IIGObservationType iig_obs_type)
          : Observer(/*has_string=*/true, /*has_tensor=*/true),
            iig_obs_type_(iig_obs_type) {}

      void WriteTensor(const State &observed_state, int player,
                       Allocator *allocator) const override
      {
        const FrenchTarotState &state =
            open_spiel::down_cast<const FrenchTarotState &>(observed_state);
      }

      std::string StringFrom(const State &observed_state,
                             int player) const override
      {
        const FrenchTarotState &state =
            open_spiel::down_cast<const FrenchTarotState &>(observed_state);
        return state.ToString();
      }

    private:
      IIGObservationType iig_obs_type_;
    };

    FrenchTarotState::FrenchTarotState(std::shared_ptr<const Game> game)
        : State(game)
    {
      phase_ = Phase::Dealing;
      current_player_ = 0;
      current_trick_ = {};
      tricks_ = {};
      deck_ = {};
      for (int i = 0; i < kDeckSize; ++i)
        deck_[i] = i;
      std::shuffle(deck_.begin(), deck_.end(), game->GetRNGState());
      slam_declare_ = Declare::NoSlam;
      player_declares_ = std::vector<Declare>(game->NumPlayers(), Declare::NoSlam);
      fool_player_ = kInvalidPlayer;
      fool_trick_ = nullptr;
      replacement_trick_ = nullptr;
      replacement_card_ = -1;
      slam_bonus_ = 0.0;
      petit_au_bout_bonus_ = 0.0;
    }

    int FrenchTarotState::CurrentPlayer() const
    {
      if (IsTerminal())
        return kTerminalPlayerId;
      if (phase_ != Phase::Playing)
        return kChancePlayerId;
      return current_player_;
    }

    void FrenchTarotState::DoApplyAction(Action move)
    {
      history_.push_back({CurrentPlayer(), move});
    }

    std::vector<Action> FrenchTarotState::LegalActions() const
    {
      switch (phase_)
      {
      case Phase::Dealing:
        return {};
      case Phase::Bidding:
        return {};
      case Phase::DeclaringPoignee:
        return {};
      case Phase::DeclaringSlam:
        return {};
      case Phase::Playing:
        return {};
      default:
        break;
      }
      return {};
    }

    std::string FrenchTarotState::ActionToString(Player player, Action action) const
    {
      if (action < kDeckSize)
      {
        std::string card_str = "";
        if (action < 56)
        {
          absl::StrAppend(&card_str, kRankStr[action % kCardsPerSuit]);
          absl::StrAppend(&card_str, kSuitsStr[action / kCardsPerSuit]);
        }
        else
        {
          absl::StrAppend(&card_str, kTrumpStr[action - kCardsPerSuit * 4]);
          absl::StrAppend(&card_str, kSuitsStr[CardSuit::Trumps]);
        }
        return absl::StrCat("[", player, ", ", card_str, "]");
      }
      else if (action < BidType::GuardAgainst)
      {
        std::string bid_str = "";
        switch (BidType(action))
        {
        case BidType::Pass:
          bid_str = "Pass";
          break;
        case BidType::Small:
          bid_str = "Small";
          break;
        case BidType::Guard:
          bid_str = "Guard";
          break;
        case BidType::GuardWithout:
          bid_str = "Guard Without";
          break;
        case BidType::GuardAgainst:
          bid_str = "Guard Against";
          break;
        default:
          bid_str = "Unknown";
          break;
        }
        return absl::StrCat("[", player, ", ", bid_str, "]");
      }
      else if (action < Declare::Slam)
      {
        std::string declare_str = "";
        switch (Declare(action))
        {
        case Declare::NoSlam:
          declare_str = "No Slam";
          break;
        case Declare::Slam:
          declare_str = "Slam";
          break;
          case Declare::Poignee:
          declare_str = "Poignee";
          break;
        case Declare::NoPoignee:
          declare_str = "No Poignee";
        default:
          declare_str = "Unknown";
          break;
        }
        return absl::StrCat("[", player, ", ", declare_str, "]");
      }
      return "Unknown";
    }

    std::string FrenchTarotState::ToString() const
    {
      std::string str = absl::StrCat("Phase: ", phase_, "\n");
      return str;
    }

    bool FrenchTarotState::IsTerminal() const
    {
      return phase_ == Phase::Terminal;
    }

    std::vector<double> FrenchTarotState::Returns() const
    {
      if (!IsTerminal())
        return std::vector<double>(num_players_, 0.0);

      std::vector<double> returns(num_players_);

      std::tuple<double, bool> score = PartialScore();
      bool taker_won = std::get<1>(score);

      double taker_score = taker_won ? GetGame()->MaxUtility() : GetGame()->MinUtility();

      for (auto player = Player{0}; player < num_players_; ++player)
      {
        if (player == taker_)
          returns[player] = taker_won ? kUtility : -kUtility;
        else
          returns[player] = taker_won ? -kUtility / (num_players_ - 1) : kUtility / (num_players_ - 1);
      }
      return returns;
    }

    std::tuple<double, bool> FrenchTarotState::PartialScore() const
    {
      return std::make_tuple(0.0, false);
    }

    std::string FrenchTarotState::InformationStateString(Player player) const
    {
      const FrenchTarotGame &game = open_spiel::down_cast<const FrenchTarotGame &>(*game_);
      return game.info_state_observer_->StringFrom(*this, player);
    }

    std::string FrenchTarotState::ObservationString(Player player) const
    {
      const FrenchTarotGame &game = open_spiel::down_cast<const FrenchTarotGame &>(*game_);
      return game.default_observer_->StringFrom(*this, player);
    }

    void FrenchTarotState::InformationStateTensor(Player player,
                                                  absl::Span<float> values) const
    {
      ContiguousAllocator allocator(values);
      const FrenchTarotGame &game = open_spiel::down_cast<const FrenchTarotGame &>(*game_);
      game.info_state_observer_->WriteTensor(*this, player, &allocator);
    }

    void FrenchTarotState::ObservationTensor(Player player,
                                             absl::Span<float> values) const
    {
      ContiguousAllocator allocator(values);
      const FrenchTarotGame &game = open_spiel::down_cast<const FrenchTarotGame &>(*game_);
      game.default_observer_->WriteTensor(*this, player, &allocator);
    }

    std::unique_ptr<State> FrenchTarotState::Clone() const
    {
      return std::unique_ptr<State>(new FrenchTarotState(*this));
    }

    std::vector<std::pair<Action, double>> FrenchTarotState::ChanceOutcomes() const
    {
      SPIEL_CHECK_TRUE(IsChanceNode());
      std::vector<std::pair<Action, double>> outcomes;

      return outcomes;
    }

    std::unique_ptr<State> FrenchTarotState::ResampleFromInfostate(
        int player_id, std::function<double()> rng) const
    {
      std::unique_ptr<State> state = game_->NewInitialState();
      Action player_chance = history_.at(player_id).action;
      for (int p = 0; p < game_->NumPlayers(); ++p)
      {
        if (p == history_.size())
          return state;
        if (p == player_id)
        {
          state->ApplyAction(player_chance);
        }
        else
        {
          Action other_chance = player_chance;
          while (other_chance == player_chance)
          {
            other_chance = SampleAction(state->ChanceOutcomes(), rng()).first;
          }
          state->ApplyAction(other_chance);
        }
      }
      SPIEL_CHECK_GE(state->CurrentPlayer(), 0);
      if (game_->NumPlayers() == history_.size())
        return state;
      for (int i = game_->NumPlayers(); i < history_.size(); ++i)
      {
        state->ApplyAction(history_.at(i).action);
      }
      return state;
    }

    FrenchTarotGame::FrenchTarotGame(const GameParameters &params)
        : Game(kGameType, params), num_players_(ParameterValue<int>("players"))
    {
      SPIEL_CHECK_GE(num_players_, kGameType.min_num_players);
      SPIEL_CHECK_LE(num_players_, kGameType.max_num_players);
      default_observer_ = std::make_shared<FrenchTarotObserver>(kDefaultObsType);
      info_state_observer_ = std::make_shared<FrenchTarotObserver>(kInfoStateObsType);
      private_observer_ = std::make_shared<FrenchTarotObserver>(
          IIGObservationType{/*public_info*/ false,
                             /*perfect_recall*/ false,
                             /*private_info*/ PrivateInfoType::kSinglePlayer});
      public_observer_ = std::make_shared<FrenchTarotObserver>(
          IIGObservationType{/*public_info*/ true,
                             /*perfect_recall*/ false,
                             /*private_info*/ PrivateInfoType::kNone});
    }

    std::unique_ptr<State> FrenchTarotGame::NewInitialState() const
    {
      return std::unique_ptr<State>(new FrenchTarotState(shared_from_this()));
    }

    std::vector<int> FrenchTarotGame::InformationStateTensorShape() const
    {
      int trick_index = num_players_ - 3;
      int hand_size = kNumTricks[trick_index];
      int tricks = kNumTricks[trick_index];
      int bid_size = num_players_;
      int declare_size = num_players_ + 1; // Handful + Slam Declare
      return {hand_size, bid_size, declare_size, tricks};
    }

    std::vector<int> FrenchTarotGame::ObservationTensorShape() const
    {
      int trick_index = num_players_ - 3;
      int hand_size = kNumTricks[trick_index];
      int tricks = kNumTricks[trick_index];
      return {hand_size, tricks};
    }

    std::shared_ptr<Observer> FrenchTarotGame::MakeObserver(
        absl::optional<IIGObservationType> iig_obs_type,
        const GameParameters &params) const
    {
      if (params.empty())
        return std::make_shared<FrenchTarotObserver>(
            iig_obs_type.value_or(kDefaultObsType));
      else
        return MakeRegisteredObserver(iig_obs_type, params);
    }
    std::string FrenchTarotGame::GetRNGState() const
    {
      std::ostringstream rng_stream;
      rng_stream << rng_;
      return rng_stream.str();
    }

    void FrenchTarotGame::SetRNGState(const std::string &rng_state) const
    {
      if (rng_state.empty())
        return;
      std::istringstream rng_stream(rng_state);
      rng_stream >> rng_;
    }

    int FrenchTarotGame::RNG() const { return rng_(); }
  } // namespace french_tarot
} // namespace open_spiel
