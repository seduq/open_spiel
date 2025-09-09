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

#pragma region Game Parameters

    namespace
    {
      const GameType kGameType{
          /*short_name=*/"french_tarot",
          /*long_name=*/"French Tarot",
          GameType::Dynamics::kSequential,
          GameType::ChanceMode::kExplicitStochastic,
          GameType::Information::kImperfectInformation,
          GameType::Utility::kZeroSum,
          GameType::RewardModel::kTerminal,
          /*max_num_players=*/kMaxNumPlayers,
          /*min_num_players=*/kMinNumPlayers,
          /*provides_information_state_string=*/true,
          /*provides_information_state_tensor=*/true,
          /*provides_observation_string=*/true,
          /*provides_observation_tensor=*/true,
          /*parameter_specification=*/
          {{"players", GameParameter(kMaxNumPlayers)},
           {"rng_seed", GameParameter(-1)}},
          /*default_loadable=*/true,
          /*provides_factored_observation_string=*/true,
      };

      std::shared_ptr<const Game> Factory(const GameParameters &params)
      {
        return std::shared_ptr<const Game>(new FrenchTarotGame(params));
      }

      REGISTER_SPIEL_GAME(kGameType, Factory);

      open_spiel::RegisterSingleTensorObserver single_tensor(kGameType.short_name);
    } // namespace game register

#pragma endregion

#pragma region State Constructor

    FrenchTarotState::FrenchTarotState(std::shared_ptr<const Game> game)
        : State(game), current_trick_(game->NumPlayers()), dog_({}),
          phase_(Phase::Dealing), fool_player_(kInvalidPlayer),
          fool_trick_(nullptr), replacement_trick_(nullptr), tricks_({}),
          replacement_card_(-1), current_player_(0), taker_(kInvalidPlayer),
          bid_(Bid::Invalid), slam_bonus_(0.0), petit_au_bout_bonus_(0.0),
          slam_declare_(Declare::NoSlam), know_cards_(kDeckSize, kInvalidPlayer),
          player_declares_(game->NumPlayers(), Declare::NoSlam) {}

#pragma endregion

#pragma region State Methods

    std::vector<double> FrenchTarotState::Returns() const
    {
      if (!IsTerminal())
        return std::vector<double>(num_players_, 0.0);

      std::vector<double> returns(num_players_);

      auto score = PartialScore();
      auto taker_won = score.second;

      auto taker_points = taker_won ? GetGame()->MaxUtility() : GetGame()->MinUtility();
      auto defenders_points = -taker_points / (num_players_ - 1);

      for (auto player = Player{0}; player < num_players_; ++player)
      {
        if (player == taker_)
          returns[player] = taker_points;
        else
          returns[player] = defenders_points;
      }
      return returns;
    }

    int FrenchTarotState::CurrentPlayer() const
    {
      if (IsTerminal())
        return kTerminalPlayerId;
      if (phase_ != Phase::Playing)
        return kChancePlayerId;
      return current_player_;
    }

    void FrenchTarotState::DealCards()
    {
      auto game = std::static_pointer_cast<const FrenchTarotGame>(GetGame());
      std::iota(deck_.begin(), deck_.end(), 0);
      std::shuffle(deck_.begin(), deck_.end(), *game->RNG());
      std::copy(deck_.begin(), deck_.begin() + kDogSize, dog_.begin());
      auto it = deck_.begin() + kDogSize;
      auto cards_per_player = (kDeckSize - kDogSize) / game->NumPlayers();
      for (auto player = Player{0}; player < game->NumPlayers(); ++player)
      {
        player_hands_[player].insert(player_hands_[player].end(), it, it + cards_per_player);
        it += cards_per_player;
      }
    }

#pragma endregion

#pragma region Legal Actions

    std::vector<Action> FrenchTarotState::LegalActions() const
    {
      switch (phase_)
      {
      case Phase::Dealing:
        return {0}; // Dummy action to deal cards
      case Phase::Bidding:
        return LegalActionsBid();
      case Phase::DeclaringPoignee:
        return LegalActionsDog();
      case Phase::DeclaringSlam:
        return LegalActionsSlam();
      case Phase::Playing:
        return LegalActionsPlay();
      default:
        break;
      }
      return {};
    }

    std::vector<Action> FrenchTarotState::LegalActionsBid() const
    {
      std::vector<Action> bids = {Bid::Pass};
      switch (bid_)
      {
      case Bid::Invalid:
      case Bid::Pass:
        bids.push_back(Bid::Small);
        bids.push_back(Bid::Guard);
        bids.push_back(Bid::GuardWithout);
        bids.push_back(Bid::GuardAgainst);
        break;
      case Bid::Small:
        bids.push_back(Bid::Guard);
        bids.push_back(Bid::GuardWithout);
        bids.push_back(Bid::GuardAgainst);
        break;
      case Bid::Guard:
        bids.push_back(Bid::GuardWithout);
        bids.push_back(Bid::GuardAgainst);
        break;
      case Bid::GuardWithout:
        bids.push_back(Bid::GuardAgainst);
        break;
      case Bid::GuardAgainst:
      default:
        break;
      }
      return bids;
    }

    std::vector<Action> FrenchTarotState::LegalActionsDog() const
    {
      auto hand = player_hands_[current_player_];
      auto filter = std::remove_if(hand.begin(), hand.end(),
                                   [](Card card)
                                   { return kDiscardFilter.count(card) > 0; });

      hand.erase(filter, hand.end());
      std::vector<Action> legal_actions(hand.begin(), hand.end());
      return legal_actions;
    }

    std::vector<Action> FrenchTarotState::LegalActionsSlam() const
    {
      if (taker_ != current_player_)
        return {Declare::NoSlam};
      return {Declare::NoSlam, Declare::Slam};
    }

    std::vector<Action> FrenchTarotState::LegalActionsHandful() const
    {
      auto handful_idx = num_players_ - kMinNumPlayers;
      if (kHandfulThreshold[handful_idx] < 0)
        return {Declare::NoPoignee};
      return {Declare::NoPoignee, Declare::Poignee};
    }

    std::vector<Action> FrenchTarotState::LegalActionsPlay() const
    {
      auto hand = player_hands_[current_player_];
      auto suit_led = current_trick_.SuitLed();
      std::vector<Card> higher_trump_cards;
      std::vector<Card> trump_cards;
      auto highest_rank = current_trick_.HighestRank();
      std::vector<Card> suit_cards;
      std::vector<Card> discard_cards;
      std::vector<Action> legal_actions;
      for (auto card : hand)
      {
        if (suit_led == Suit::Trumps && CardTrumpRank(card) > highest_rank)
          higher_trump_cards.push_back(card);
        if (suit_led == CardSuit(card))
          suit_cards.push_back(card);
        if (suit_led != Suit::Trumps && CardSuit(card) == Suit::Trumps)
          trump_cards.push_back(card);
        discard_cards.push_back(card);
      }
      // If we are following trumps and have a higher card, we must play it
      if (!higher_trump_cards.empty())
        legal_actions.insert(legal_actions.end(), higher_trump_cards.begin(), higher_trump_cards.end());

      // If we are following trumps and don't have a higher trump, we must play a trump
      if (legal_actions.empty())
        legal_actions.insert(legal_actions.end(), trump_cards.begin(), trump_cards.end());

      // If we don't have trumps, we must follow suit
      if (legal_actions.empty())
        legal_actions.insert(legal_actions.end(), suit_cards.begin(), suit_cards.end());

      // If we don't have a valid card to play, we must discard
      if (legal_actions.empty())
        legal_actions.insert(legal_actions.end(), discard_cards.begin(), discard_cards.end());

      // The fool can be played at any time
      if (std::find(legal_actions.begin(), legal_actions.end(), kFool) == legal_actions.end() &&
          std::find(hand.begin(), hand.end(), kFool) != hand.end())
        legal_actions.push_back(kFool);

      return legal_actions;
    }

#pragma endregion

#pragma region Apply Action

    /**
     * @brief Applies the given action to the current game state.
     * @param move The action to apply to the state.
     *
     * @details The actions vary depending on the current game phase:
     *
     * **Phase-specific actions:**
     * - Dealing phase: distribute cards to players and the dog
     * - Bidding phase: players bid in turn
     * - DeclaringSlam phase: the taker declares whether they intend to make a slam
     * - DeclaringPoignee phase: players declare whether they have a poignee
     * - Playing phase: players play cards in turn
     *
     * **Action value ranges:**
     *
     * Card actions (0-77):
     * - 0-13: Hearts cards
     * - 14-27: Diamonds cards
     * - 28-41: Clubs cards
     * - 42-55: Spades cards
     * - 56-77: Trump cards
     *
     * Bidding actions (78-82):
     * - 78: Bid Pass
     * - 79: Bid Small
     * - 80: Bid Guard
     * - 81: Bid Guard Without
     * - 82: Bid Guard Against
     *
     * Declaration actions (83-86):
     * - 83: Declare No Poignee
     * - 84: Declare Poignee
     * - 85: Declare No Slam
     * - 86: Declare Slam
     */
    void FrenchTarotState::DoApplyAction(Action move)
    {
      switch (phase_)
      {
      case Phase::Dealing:
        DealCards();
        break;
      case Phase::Bidding:
        ApplyActionBid(static_cast<Bid>(move));
        break;
      case Phase::DeclaringSlam:
        ApplyActionSlam(static_cast<Declare>(move));
        break;
      case Phase::DeclaringPoignee:
        ApplyActionDog(static_cast<Card>(move));
        break;
      case Phase::Playing:
        ApplyActionPlay(static_cast<Card>(move));
        break;
      default:
        return SpielFatalError("Invalid phase in ApplyAction");
      }
      history_.push_back({CurrentPlayer(), move});
    }

    void FrenchTarotState::ApplyActionBid(Bid bid) const
    {
      if (bid < Bid::Pass || bid > Bid::GuardAgainst)
        return SpielFatalError("Invalid bid action");
      if (bid != Bid::Pass)
      {
      }
    }

    void FrenchTarotState::ApplyActionDog(Card card) const {}

    void FrenchTarotState::ApplyActionSlam(Declare declare) const {}

    void FrenchTarotState::ApplyActionHandful(Declare declare) const {}

    void FrenchTarotState::ApplyActionPlay(Card card) const {}

#pragma endregion

#pragma region Chance Outcomes

    ActionsAndProbs FrenchTarotState::ChanceOutcomes() const {}

    ActionsAndProbs FrenchTarotState::ChanceBidActions() {}

    ActionsAndProbs FrenchTarotState::ChanceDogActions() {}

    ActionsAndProbs FrenchTarotState::ChanceSlamActions() {}

    ActionsAndProbs FrenchTarotState::ChanceHandfulActions() {}

#pragma endregion

#pragma region Scoring

    /** @brief Computes the partial score for the taker.
     *  @return A pair containing the score and a boolean indicating if the taker won.
     *
     *  @details This function calculates the score for the taker based on the cards
     *  they have collected in tricks. It returns a pair where the first element is
     *  the score (positive if the taker won, negative otherwise) and the second
     *  element is a boolean indicating whether the taker won or not.
     */
    std::pair<double, bool> FrenchTarotState::PartialScore() const
    {
      // TODO: Implement the actual scoring logic based on the rules of French Tarot.
      return std::make_pair(0.0, false);
    }

    void FrenchTarotState::SettleFool() const {

    };

    void FrenchTarotState::FindReplacement() const {

    };

    double FrenchTarotState::PetitBonus() const {

    };

    double FrenchTarotState::SlamBonus() const {

    };

#pragma endregion

#pragma region Show Cards

    void FrenchTarotState::ShowDog() const {}
    void FrenchTarotState::ShowHandful(Player player) const {}

#pragma endregion

#pragma region Observers and Information State

    /** @brief Observer for French Tarot.
     */
    class FrenchTarotObserver : public Observer
    {
    public:
      FrenchTarotObserver(IIGObservationType iig_obs_type)
          : Observer(/*has_string=*/true, /*has_tensor=*/true),
            iig_obs_type_(iig_obs_type) {}

      /** @brief Writes the tensor representation of the observed state.
       *  @param observed_state The observed state.
       *  @param player The player for whom the observation is made.
       *  @param allocator The allocator to use for the tensor.
       */
      void WriteTensor(const State &observed_state, int player,
                       Allocator *allocator) const override
      {
        const FrenchTarotState &state =
            open_spiel::down_cast<const FrenchTarotState &>(observed_state);
      }

      /** @brief Converts the observed state to a string representation.
       *  @param observed_state The observed state.
       *  @param player The player for whom the observation is made.
       *  @return The string representation of the observed state.
       */
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

    std::unique_ptr<State> FrenchTarotState::ResampleFromInfostate(
        int player_id, std::function<double()> rng) const
    {
      std::unique_ptr<State> state = game_->NewInitialState();

      return state;
    }

    std::unique_ptr<State> FrenchTarotState::Clone() const
    {
      return std::unique_ptr<State>(new FrenchTarotState(*this));
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
          absl::StrAppend(&card_str, kSuitsStr[Suit::Trumps]);
        }
        return absl::StrCat("[", player, ", ", card_str, "]");
      }
      else if (action < Bid::GuardAgainst)
      {
        std::string bid_str = "";
        switch (Bid(action))
        {
        case Bid::Pass:
          bid_str = "Pass";
          break;
        case Bid::Small:
          bid_str = "Small";
          break;
        case Bid::Guard:
          bid_str = "Guard";
          break;
        case Bid::GuardWithout:
          bid_str = "Guard Without";
          break;
        case Bid::GuardAgainst:
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

#pragma endregion

#pragma region Game Methods

    FrenchTarotGame::FrenchTarotGame(const GameParameters &params)
        : Game(kGameType, params),
          num_players_(ParameterValue<int>("players")),
          seed_(ParameterValue<int>("rng_seed", kDefaultSeed)),
          rng_(new std::mt19937(seed_ >= 0 ? seed_ : kDefaultSeed))
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
      int num_players_idx = num_players_ - kMinNumPlayers;
      int hand_size = kNumTricks[num_players_idx];
      int trick_size = kNumTricks[num_players_idx];
      int bid_size = num_players_;
      int declare_size = num_players_ + 1;
      int dog_observe_size = kDogSize;
      int dog_discard_size = kDogSize;
      return {hand_size, bid_size,
              dog_observe_size, dog_discard_size,
              declare_size, trick_size * kTrickSize};
    }

    std::vector<int> FrenchTarotGame::ObservationTensorShape() const
    {
      int num_players_idx = num_players_ - kMinNumPlayers;
      int hand_size = kNumTricks[num_players_idx];
      int trick_size = kNumTricks[num_players_idx];
      int dog_observe_size = kDogSize;
      return {hand_size, dog_observe_size, trick_size * kTrickSize};
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
      rng_stream << *rng_;
      return rng_stream.str();
    }

    void FrenchTarotGame::SetRNGState(const std::string &rng_state) const
    {
      if (rng_state.empty())
        return;
      std::istringstream rng_stream(rng_state);
      rng_stream >> *rng_;
    }

#pragma endregion

#pragma region Trick

    void Trick::Play(Player player, Card card)
    {
      if (cards_.size() == num_players_)
        return;

      cards_.push_back(std::make_pair(player, card));
      auto suit = Suit(card / kCardsPerSuit);
      auto rank = card % kCardsPerSuit;
      if (suit == Suit::Trumps)
        rank = card - kCardsPerSuit * (kNumSuits - 1);

      if (suit == Suit::Trumps && rank == Trump::Fool)
        return;

      if (leader_ == -1)
      {
        leader_ = player;
        suit_ = suit;
        winner_ = player;
        highest_rank_ = rank;
        return;
      }

      if (suit_ != Suit::Trumps && suit == Suit::Trumps)
      {
        suit_ = suit;
        winner_ = player;
        highest_rank_ = rank;
      }
      else if (suit == suit_ && rank > highest_rank_)
      {
        highest_rank_ = rank;
        winner_ = player;
      }
    }

    void Trick::ReplaceFool(Player player, Card card)
    {
      points_ -= CardPoints(kCardsPerSuit * (kNumSuits - 1) + Trump::Fool);
      for (auto &p : cards_)
      {
        if (p.first == player && p.second == 0)
        {
          p.second = card;
          return;
        }
      }
    }

    std::vector<int> Trick::Tensor()
    {
      auto tensor = std::vector<int>(kTrickSize, -1);
      auto i = 0;
      for (const auto &p : cards_)
        tensor[leader_ + i++] = p.second;
      return tensor;
    }

    std::string Trick::ToString()
    {
      std::string result = "P-" + std::to_string(leader_) + "|";

      for (const auto &[player, card] : cards_)
      {
        if (player == -1)
          continue;

        if (card >= 0 && card < kDeckSize)
        {
          const int suit = card / kCardsPerSuit;
          const int rank = card % kCardsPerSuit;
          result += kRankStr[rank];
          result += kSuitsStr[suit];
          result += "|";
        }
        else
        {
          result += "??|";
        }
      }
      return result;
    }

#pragma endregion

  } // namespace french_tarot
} // namespace open_spiel
