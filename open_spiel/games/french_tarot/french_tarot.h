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

#ifndef OPEN_SPIEL_GAMES_FRENCH_TAROT_H_
#define OPEN_SPIEL_GAMES_FRENCH_TAROT_H_

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "open_spiel/policy.h"
#include "open_spiel/spiel.h"
#include "open_spiel/spiel_utils.h"

namespace open_spiel
{
  namespace french_tarot
  {
    inline constexpr const int kDefaultNumPlayers = 4;
    inline constexpr const int kMaxNumPlayers = 4;
    inline constexpr const int kMinNumPlayers = 3;
    inline constexpr const int kDeckSize = 78;
    inline constexpr const int kDogSize = 6;
    inline constexpr const int kDogId = 4;
    inline constexpr const int kDealSize = kDeckSize;
    inline constexpr const int kTrickSize = kDeckSize;
    inline constexpr const double kUtility = 6.0;
    inline constexpr const std::array<int, 2> kNumTricks = {
        24, // 3 players
        18  // 4 players
    };
    inline constexpr const int kCardsPerSuit = 14;
    inline constexpr const std::array<int, 7> kBidMultipliers = {
        0, // Pass
        1, // Small
        2, // Guard
        4, // Guard Without
        6  // Guard Against
    };

    inline constexpr const std::array<int, kDeckSize> deck = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,           // Hearts
        14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, // Diamonds
        28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, // Clubs
        42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, // Spades
        // Trumps
        56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69,
        70, 71, 72, 73, 74, 75, 76, 77};

    inline constexpr const std::array<const char *, 5> kSuitsStr = {"♥", "♦", "♣", "♠", "§"};
    inline constexpr const std::array<const char *, 14> kRankStr = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "C", "Q", "K"};
    inline constexpr const std::array<const char *, 22> kTrumpStr = {
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10",
        "11", "12", "13", "14", "15", "16", "17", "18", "19", "20", "21"};

    enum CardSuit
    {
      Hearts,
      Diamonds,
      Clubs,
      Spades,
      Trumps
    };

    enum CardRank
    {
      Number,
      Jack,
      Knight,
      Queen,
      King
    };

    enum BidType
    {
      Pass = 78,
      Small = 79,
      Guard = 80,
      GuardWithout = 81,
      GuardAgainst = 82,
      size = 5
    };

    enum Declare
    {
      NoPoignee = 83,
      Poignee = 84,
      NoSlam = 85,
      Slam = 86,
      size = 4
    };

    enum Phase
    {
      Dealing,
      Bidding,
      DeclaringSlam,
      DeclaringPoignee,
      Playing,
      Terminal
    };

    class FrenchTarotGame;
    class FrenchTarotObserver;

    using Card = int;
    using Trick = std::vector<std::pair<Player, Card>>;

    class FrenchTarotState : public State
    {
    public:
      explicit FrenchTarotState(std::shared_ptr<const Game> game);
      FrenchTarotState(const FrenchTarotState &) = default;

      Player CurrentPlayer() const override;

      std::vector<Action> LegalActions() const override;
      std::vector<std::pair<Action, double>> ChanceOutcomes() const override;
      bool IsTerminal() const override;
      std::vector<double> Returns() const override;
      std::tuple<double, bool> PartialScore() const;
      std::string ActionToString(Player player, Action move) const override;
      std::string ToString() const override;
      std::string InformationStateString(Player player) const override;
      std::string ObservationString(Player player) const override;
      void InformationStateTensor(Player player,
                                  absl::Span<float> values) const override;
      void ObservationTensor(Player player,
                             absl::Span<float> values) const override;
      std::unique_ptr<State> Clone() const override;
      std::unique_ptr<State> ResampleFromInfostate(
          int player_id, std::function<double()> rng) const override;

    protected:
      void DoApplyAction(Action move) override;

    private:
      friend class FrenchTarotObserver;
      Phase phase_;
      Player current_player_;
      Player taker_;
      BidType bid_;
      std::vector<BidType> player_bids_;

      std::vector<std::vector<Card>> player_hands_;
      std::array<Card, kDogSize> dog_;
      std::vector<Card> discard_;

      Trick current_trick_;
      std::vector<Trick> tricks_;
      std::array<Card, kDeckSize> deck_;

      Declare slam_declare_;
      std::vector<Declare> player_declares_;

      Player fool_player_;
      Trick *fool_trick_;
      Trick *replacement_trick_;
      Card replacement_card_;

      double slam_bonus_;
      double petit_au_bout_bonus_;
    };

    class FrenchTarotGame : public Game
    {
    public:
      explicit FrenchTarotGame(const GameParameters &params);
      int NumDistinctActions() const override
      {
        int kBidSize = static_cast<int>(BidType::size);
        int kDeclareSize = static_cast<int>(Declare::size);
        return (kDeckSize + kBidSize + kDeclareSize);
      }
      std::unique_ptr<State> NewInitialState() const override;
      int MaxChanceOutcomes() const override
      {
        int kBidSize = num_players_;
        int kDeclareSize = num_players_ + 1;
        return (kDeckSize + kBidSize + kDeclareSize);
      }
      int NumPlayers() const override { return num_players_; }
      double MinUtility() const override { return -kUtility; }; // Support 3 and 4 players
      double MaxUtility() const override { return kUtility; };  // [6, 3, 3] or [6, 2, 2, 2]
      absl::optional<double> UtilitySum() const override { return 0; }
      std::vector<int> InformationStateTensorShape() const override;
      std::vector<int> ObservationTensorShape() const override;
      int MaxGameLength() const override
      {
        int kBidSize = num_players_;
        int kDeclareSize = num_players_ + 1; // Handful + Slam Declare
        return (kDealSize + kBidSize + kDeclareSize + kTrickSize);
      }
      int MaxChanceNodesInHistory() const override
      {
        int kBidSize = num_players_;
        int kDeclareSize = num_players_ + 1; // Handful + Slam Declare
        return (kDealSize + kBidSize + kDeclareSize);
      }

      std::string GetRNGState() const override;
      void SetRNGState(const std::string &rng_state) const override;

      std::shared_ptr<Observer> MakeObserver(
          absl::optional<IIGObservationType> iig_obs_type,
          const GameParameters &params) const override;

      std::shared_ptr<FrenchTarotObserver> default_observer_;
      std::shared_ptr<FrenchTarotObserver> info_state_observer_;
      std::shared_ptr<FrenchTarotObserver> public_observer_;
      std::shared_ptr<FrenchTarotObserver> private_observer_;

    private:
      int num_players_;
      mutable std::mt19937 rng_;
      int RNG() const;
    };

  } // namespace french_tarot
} // namespace open_spiel

#endif // OPEN_SPIEL_GAMES_FRENCH_TAROT_H_
