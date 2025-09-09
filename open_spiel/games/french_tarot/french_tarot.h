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
    using Card = int;
    class FrenchTarotGame;
    class FrenchTarotObserver;

    inline constexpr const int kMaxNumPlayers = 4;
    inline constexpr const int kMinNumPlayers = 3;
    inline constexpr const int kDeckSize = 78;
    inline constexpr const int kDogSize = 6;
    inline constexpr const int kDogId = 4;
    inline constexpr const int kTrickSize = 7;
    inline constexpr const double kUtility = 6.0;
    inline constexpr const int kNumBids = 5;
    inline constexpr const int kNumDeclares = 4;
    inline constexpr const int kCardsPerSuit = 14;
    inline constexpr const int kCardsOfTrump = 22;
    inline constexpr const int kNumSuits = 5;
    inline constexpr const int kFool = 56;
    inline constexpr const int kDefaultSeed = 42;

    inline constexpr const std::array<int, 2> kNumTricks = {
        24, // 3 players
        18  // 4 players
    };
    inline constexpr const std::array<int, kNumBids> kBidMultipliers = {
        1, // Pass
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

    const std::array<Card, kCardsOfTrump> kTrumps = []
    {
      std::array<Card, kCardsOfTrump> trumps{};
      std::iota(trumps.begin(), trumps.end(), kFool);
      return trumps;
    }();

    const std::array<Card, kNumSuits> kKings = []
    {
      std::array<Card, kNumSuits> kings{};
      for (int suit = 0; suit < kNumSuits - 1; ++suit)
        kings[suit] = suit * kCardsPerSuit + Rank::King;
      return kings;
    }();

    const std::set<Card> kDiscardFilter = []
    {
      std::set<Card> to_remove;
      to_remove.insert(kTrumps.begin(), kTrumps.end());
      to_remove.insert(kKings.begin(), kKings.end());
      return to_remove;
    }();

    inline constexpr const std::array<const char *, kNumSuits> kSuitsStr = {"♥", "♦", "♣", "♠", "§"};
    inline constexpr const std::array<const char *, kCardsPerSuit> kRankStr = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "C", "Q", "K"};
    inline constexpr const std::array<const char *, kCardsOfTrump> kTrumpStr = {
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10",
        "11", "12", "13", "14", "15", "16", "17", "18", "19", "20", "21"};

    inline constexpr const std::array<int, 3> kHandfulThreshold = {
        13,
        10,
    };
    inline constexpr const std::array<std::array<int, 3>, 3> kHandfulBonus = {
        std::array<int, 3>{10, 13, 20},
        std::array<int, 3>{13, 15, 30},
        std::array<int, 3>{15, 18, 40},
    };

    enum Suit : int
    {
      Invalid = -1,
      Hearts = 0,
      Diamonds = 1,
      Clubs = 2,
      Spades = 3,
      Trumps = 4
    };

    enum Rank : int
    {
      Invalid = -1,
      Jack = 10,
      Knight = 11,
      Queen = 12,
      King = 13,
    };

    enum Trump : int
    {
      Invalid = -1,
      Fool = 0,
      Petit = 1,
      World = 21
    };

    enum Bid : int
    {
      Invalid = -1,
      Pass = 78,
      Small = 79,
      Guard = 80,
      GuardWithout = 81,
      GuardAgainst = 82,
      size = 5
    };

    enum Declare : int
    {
      Invalid = -1,
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
      Discarding,
      DeclaringSlam,
      DeclaringPoignee,
      Playing,
      Terminal
    };

    double CardPoints(Card card)
    {
      if (card < 0 || card >= kDeckSize)
        return 0.0;
      auto suit = Suit(card / kCardsPerSuit);
      if (suit == Suit::Trumps)
      {
        auto rank = card - kCardsPerSuit * (kNumSuits - 1);
        if (rank == Trump::Fool)
          return 4.5;
        if (rank == Trump::Fool)
          return 4.5;
        else if (rank == Trump::World)
          return 4.5;
        else if (rank == Trump::Petit)
          return 4.5;
        else
          return 0.5;
      }
      else
      {
        auto rank = card % kCardsPerSuit;
        if (rank == Rank::King)
          return 4.5;
        else if (rank == Rank::Queen)
          return 3.5;
        else if (rank == Rank::Knight)
          return 2.5;
        else if (rank == Rank::Jack)
          return 1.5;
        else
          return 0.5;
      }
    };

    Rank CardRank(Card card)
    {
      if (card < 0 || card >= kFool)
        return Rank::Invalid;
      auto suit = Suit(card / kCardsPerSuit);
      if (suit != Suit::Trumps)
      {
        auto rank = card % kCardsPerSuit;
        return static_cast<Rank>(rank);
      }
      else
        return Rank::Invalid;
    };

    Trump CardTrumpRank(Card card)
    {
      if (card < 0 || card >= kDeckSize)
        return Trump::Invalid;
      auto suit = Suit(card / kCardsPerSuit);
      if (suit == Suit::Trumps)
      {
        auto rank = card - kCardsPerSuit * (kNumSuits - 1);
        if (rank >= 0 && rank <= 21)
          return static_cast<Trump>(rank);
        else
          return Trump::Invalid;
      }
      else
        return Trump::Invalid;
    }

    Suit CardSuit(Card card)
    {
      if (card < 0 || card >= kDeckSize)
        return Suit::Invalid;
      return Suit(card / kCardsPerSuit);
    };

    Trump CardTrump(Card card)
    {
      if (card < 0 || card >= kDeckSize)
        return Trump::Invalid;
      auto suit = Suit(card / kCardsPerSuit);
      if (suit == Suit::Trumps)
      {
        auto rank = card - kCardsPerSuit * (kNumSuits - 1);
        if (rank >= 0 && rank <= 21)
          return static_cast<Trump>(rank);
        else
          return Trump::Invalid;
      }
      else
      {
        return Trump::Invalid;
      }
    };

    Declare DeclareFromAction(Action action)
    {
      if (action >= Declare::NoPoignee && action < Declare::size + Declare::NoPoignee)
        return static_cast<Declare>(action);
      else
        return Declare::Invalid;
    };

    class Trick
    {
    public:
      Trick(int num_players) : leader_(kInvalidPlayer), suit_(Suit::Invalid),
                               cards_({}), points_(0.0),
                               highest_rank_(-1), num_players_(num_players),
                               winner_(kInvalidPlayer) {}
      Trick(int num_players,
            Player leader, Card card) : leader_(leader), num_players_(num_players)
      {
        cards_.push_back(std::make_pair(leader, card));
        suit_ = Suit(card / kCardsPerSuit);
        if (suit_ == Suit::Trumps)
          highest_rank_ = card - kCardsPerSuit * (kNumSuits - 1);
        else
          highest_rank_ = card % kCardsPerSuit;
      }
      Player Leader() const { return leader_; }
      Player Winner() const { return winner_; };
      double Points() const { return points_; }
      Suit SuitLed() const { return suit_; }
      int HighestRank() const { return highest_rank_; }
      const std::vector<std::pair<Player, Card>> &Cards() const { return cards_; }
      void Play(Player player, Card card);
      void ReplaceFool(Player player, Card card);
      std::vector<Card> Tensor();
      std::string ToString();

    private:
      Player leader_;
      Player winner_;
      Suit suit_;
      double points_;
      int highest_rank_;
      int num_players_;
      std::vector<std::pair<Player, Card>> cards_;
    };

    class FrenchTarotState : public State
    {
    public:
      explicit FrenchTarotState(std::shared_ptr<const Game> game);
      FrenchTarotState(const FrenchTarotState &) = default;

      bool IsTerminal() const override { return phase_ == Phase::Terminal; };
      Player CurrentPlayer() const override;
      std::vector<Action> LegalActions() const override;
      std::vector<std::pair<Action, double>> ChanceOutcomes() const override;

      std::vector<double> Returns() const override;
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
      std::pair<double, bool> PartialScore() const;

      std::vector<Action> LegalActionsBid() const;
      std::vector<Action> LegalActionsDog() const;
      std::vector<Action> LegalActionsSlam() const;
      std::vector<Action> LegalActionsHandful() const;
      std::vector<Action> LegalActionsPlay() const;

      ActionsAndProbs ChanceBidActions();
      ActionsAndProbs ChanceDogActions();
      ActionsAndProbs ChanceSlamActions();
      ActionsAndProbs ChanceHandfulActions();

      void ApplyActionBid(Bid bid) const;
      void ApplyActionDog(Card card) const;
      void ApplyActionSlam(Declare declare) const;
      void ApplyActionHandful(Declare declare) const;
      void ApplyActionPlay(Card card) const;

    private:
      void DealCards();

      void SettleFool() const;
      void FindReplacement() const;
      double PetitBonus() const;
      double SlamBonus() const;

      void ShowHandful(Player player) const;
      void ShowDog() const;

      friend class FrenchTarotObserver;
      Phase phase_;
      Player current_player_;
      Player taker_;
      Bid bid_;

      std::vector<Bid> player_bids_;
      std::vector<std::vector<Card>> player_hands_;
      std::vector<Player> know_cards_;
      std::vector<Action> player_bids_;
      std::vector<Card> dog_;
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
        auto bid_size = static_cast<int>(Bid::size);
        auto declare_size = static_cast<int>(Declare::size);
        return (kDeckSize + bid_size + declare_size);
      }
      std::unique_ptr<State> NewInitialState() const override;
      int NumPlayers() const override { return num_players_; }
      double MinUtility() const override { return -kUtility; };
      double MaxUtility() const override { return kUtility; };
      absl::optional<double> UtilitySum() const override { return 0; }
      std::vector<int> InformationStateTensorShape() const override;
      std::vector<int> ObservationTensorShape() const override;
      int MaxChanceOutcomes() const override
      {
        auto bid_size = num_players_;
        auto declare_size = num_players_ + 1;
        return (kDeckSize + bid_size + declare_size);
      }
      int MaxGameLength() const override
      {
        auto bid_size = num_players_;
        auto declare_size = num_players_ + 1; // Handful + Slam Declare
        auto num_tricks = kNumTricks[num_players_ - kMinNumPlayers];
        return (1 + bid_size + kDogSize + declare_size + (kDeckSize - kDogSize));
      }
      int MaxChanceNodesInHistory() const override
      {
        auto bid_size = num_players_;
        auto declare_size = num_players_ + 1; // Handful + Slam Declare
        return (bid_size + declare_size);
      }

      std::mt19937 *RNG() const { return rng_.get(); }
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
      mutable std::unique_ptr<std::mt19937> rng_;
      int num_players_;
      int seed_;
    };

  } // namespace french_tarot
} // namespace open_spiel

#endif // OPEN_SPIEL_GAMES_FRENCH_TAROT_H_
