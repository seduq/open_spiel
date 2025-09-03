
import numpy as np
import pyspiel  # type: ignore
from enum import Enum, EnumMeta
from typing import List, Literal, Optional, Set, Tuple, Dict
from collections.abc import Sequence
from absl import app
from absl import flags


class Phase(int, Enum):
    DEAL = 0  # Dealing phase
    BID = 1  # Contract phase
    DOG = 2  # Dog (chien) phase
    DECLARE_SLAM = 3  # Declare slam (chelem) phase
    DECLARE_HANDFUL = 4  # Declare handful (poignee) phase
    PLAY = 5  # Trick-Taking phase
    TERMINAL = 6  # Game over


class Bid(int, Enum):
    PASS = 78
    SMALL = 79
    GUARD = 80
    GUARD_WITHOUT = 81
    GUARD_AGAINST = 82


class Declaration(int, Enum):
    DECLARE_NO_HANDFUL = 83
    DECLARE_NO_SLAM = 84
    DECLARE_HANDFUL = 85
    DECLARE_SLAM = 86


class Suit(Enum):
    HEARTS = 0
    DIAMONDS = 1
    CLUBS = 2
    SPADES = 3
    TRUMPS = 4


class RankMeta(EnumMeta):
    def __new__(cls, class_name, bases, class_dict, **kwargs):
        suit_range = kwargs.get("suit_range", None)
        trump_range = kwargs.get("trump_range", None)
        bouts = [0, 1, 21]
        faces = [10, 11, 12, 13]
        if suit_range:
            for i in range(*suit_range):
                if i not in faces:
                    class_dict[f"NUMBER_{i}"] = i
        if trump_range:
            for i in range(*trump_range):
                if i not in bouts:
                    class_dict[f"TRUMP_{i}"] = i
        return super().__new__(cls, class_name, bases, class_dict)


class Rank(int, Enum, metaclass=RankMeta, suit_range=(0, 14)):
    JACK = 10
    KNIGHT = 11
    QUEEN = 12
    KING = 13


class Trump(int, Enum, metaclass=RankMeta, trump_range=(0, 22)):
    FOOL = 0
    PETIT = 1
    MONDE = 21


class Card:
    id: int
    rank: Rank | Trump
    suit: Suit
    value: float

    def __init__(self, id: int) -> None:
        self.id = id
        self.suit = Suit(min(id // _CARDS_PER_SUIT, Suit.TRUMPS.value))
        if self.suit == Suit.TRUMPS:
            self.rank = Trump(id % _CARDS_PER_SUIT)
        else:
            self.rank = Rank(id % _CARDS_PER_SUIT)
        self.value = self._value()

    def _value(self) -> float:
        if self.rank in _CARD_VALUES:
            return _CARD_VALUES[self.rank]
        return 0.5

    def __str__(self) -> str:
        if self.suit == Suit.TRUMPS:
            return f"{_TRUMP_STR[self.rank.value]}{_SUITS_STR[self.suit.value]}"
        return f"{_CARDS_STR[self.rank.value]}{_SUITS_STR[self.suit.value]}"

    def __repr__(self) -> str:
        return str(self)

    def __hash__(self) -> int:
        return self.id

    def __eq__(self, value: object) -> bool:
        if not isinstance(value, Card):
            return False
        return (self.id == value.id)


class Player:
    id: int
    name: str
    bid:  Bid | None
    slam: Declaration | None
    handful: Declaration | None
    hand: List[Card]
    tricks: List['Trick']

    def __init__(self, id: int, name: str | None = None) -> None:
        self.id = id
        if name is None:
            self.name = f"P-{id + 1}"
        else:
            self.name = name
        self.bid = None
        self.slam = None
        self.handful = None
        self.hand = []
        self.tricks = []

    def bid_multiplier(self):
        match self.bid:
            case Bid.PASS:
                return 0
            case Bid.SMALL:
                return 1
            case Bid.GUARD:
                return 2
            case Bid.GUARD_WITHOUT:
                return 4
            case Bid.GUARD_AGAINST:
                return 6
        raise ValueError("Invalid bid multiplier")

    def __str__(self) -> str:
        string = (
            f"{self.name}\tHand: [{' | '.join([str(card) for card in sorted(self.hand, key=lambda c: c.id)])}]\n"
            f"\t{self.bid} | {self.slam or Declaration.DECLARE_NO_SLAM} | {self.handful or Declaration.DECLARE_NO_HANDFUL}"
        )
        return string

    def __repr__(self) -> str:
        return str(self)


class Trick:
    plays: List[Tuple[Player, Card]]
    suit: Suit | None
    rank: Rank | Trump | None
    _winner: Player | None

    def __init__(self) -> None:
        self.plays = []
        self.dict_plays = {}
        self.suit = None
        self.rank = None
        self._winner = None

    def append(self, player: Player, card: Card):
        if (not (self.suit and
                 self.rank)):
            self.suit = card.suit
            self.rank = card.rank
            self.plays.append((player, card))
            return

        suits = set(c.suit for c in player.hand)
        has_suit = self.suit in suits
        follow_trump = (self.suit != Suit.TRUMPS and
                        card.suit == Suit.TRUMPS)
        higher_rank_trumps = [c for c in player.hand
                              if c.suit == Suit.TRUMPS and
                              c.rank > self.rank]
        discard = (follow_trump or has_suit or
                   (not follow_trump and not has_suit))

        assert len(self.plays) < _NUM_PLAYERS
        assert follow_trump or has_suit or discard
        if self.suit == Suit.TRUMPS and not discard:
            assert (card in higher_rank_trumps or
                    not higher_rank_trumps)
        elif has_suit and not discard:
            assert card.suit == self.suit

        if follow_trump:
            self.suit = Suit.TRUMPS
            self.rank = card.rank

        self.plays.append((player, card))

    def remove(self, player: Player, card: Card) -> None:
        for _play in self.plays:
            _p, _c = _play
            if player == _p and card == _c:
                self.plays.remove(_play)
                return

    def replace(self, player: Player, card: Card) -> None:
        for i, (p, c) in enumerate(self.plays):
            if p == player and c == card:
                self.plays[i] = (player, card)
                return

    def winner(self, player: Player | None = None) -> Player | None:
        if self._winner:
            return self._winner
        if player:
            self._winner = player
            return self._winner
        if len(self.plays) != _NUM_PLAYERS:
            return None
        lead_cards = [(p, c) for p, c in self.plays if c.suit == self.suit]
        self._winner, _ = max(lead_cards, key=lambda x: x[1].rank)
        return self._winner

    def set_winner(self, player: Player) -> None:
        self._winner = player

    def empty(self):
        return len(self.plays) == 0

    def __str__(self) -> str:
        string = f"Trick[{" | ".join(f"{p.name} {str(card)}" for p,
                                     card in self.plays)}]"
        if self._winner:
            string += f" | Winner: {self._winner.name}"
        return string

    def __repr__(self) -> str:
        return str(self)


# ===============
# GAME CONSTANTS
# ===============
_NUM_PLAYERS = 4
_DECK_SIZE = 78
_DOG_SIZE = 6
_DOG_ID = 4
_DECK = frozenset(list(range(_DECK_SIZE)))
_HAND_SIZE = 18
_NUM_TRICKS = _HAND_SIZE
_CARDS_PER_SUIT = 14
_CARDS_PER_DEAL = 3
_BID_MULTIPLIERS = {
    Bid.PASS: 0,
    Bid.SMALL: 1,
    Bid.GUARD: 2,
    Bid.GUARD_WITHOUT: 4,
    Bid.GUARD_AGAINST: 6
}

# ===============
# GAME ACTIONS
# ===============
_BIDS = frozenset(int(a) for a in [
    Bid.PASS, Bid.SMALL, Bid.GUARD,
    Bid.GUARD_WITHOUT, Bid.GUARD_AGAINST])
_DECLARATIONS = frozenset(int(a) for a in [
    Declaration.DECLARE_NO_HANDFUL, Declaration.DECLARE_NO_SLAM,
    Declaration.DECLARE_HANDFUL, Declaration.DECLARE_SLAM])

_DISTINCT_BIDS = len(_BIDS)
_DISTINCT_DECLARATIONS = len(_DECLARATIONS)
_DISTINCT_ACTIONS = _DECK_SIZE + _DISTINCT_BIDS + _DISTINCT_DECLARATIONS

# ===============
# Score Bonus
# ===============

# Minimum trumps required to declare a handful
# Score bonus if trump is achieved at the end
_HANDFUL_THRESHOLD = [10, 13, 15]
_HANDFUL_BONUS = [20, 30, 40]

# Points requirements per ends (bouts) cards
# Bouts are trump #0 "fool", #1 "petit" and #21 "monde"
_POINTS_REQUIRED_PER_BOUTS = [
    51,
    46,
    41,
    36,
]

# ===============
# Tensor Constants
# ===============
_TENSOR_TRICK = 7
_TENSOR_DEAL = _DECK_SIZE
_TENSOR_BID = _NUM_PLAYERS
_TENSOR_DOG = _DOG_SIZE
_TENSOR_DECLARE_SLAM = 1
_TENSOR_DECLARE_HANDFUL = _NUM_PLAYERS
_TENSOR_DECLARATIONS = _TENSOR_DECLARE_HANDFUL + _TENSOR_DECLARE_SLAM
_TENSOR_PLAY = _NUM_TRICKS * _TENSOR_TRICK
_TENSOR_SIZE = (_TENSOR_DEAL + _TENSOR_BID + _TENSOR_DOG +
                _TENSOR_DECLARATIONS + _TENSOR_PLAY)

# Game is consist of 5 phase actions
# 1. Dealing of 78 cards
# 2. Bidding of 4 players
# 3. Discarding of 6 cards, if applicable
# 4. Declaring yes or handful and/or slam
# 5. Playing the tricks
_GAME_LENGTH = (_DECK_SIZE + _NUM_PLAYERS + _DOG_SIZE +
                _NUM_PLAYERS * 2 + _DECK_SIZE)
_GAME_NAME = "french_tarot"
_GAME_FULL_NAME = "French Tarot"
_GAME_TYPE = pyspiel.GameType(
    short_name=_GAME_NAME,
    long_name=_GAME_FULL_NAME,
    dynamics=pyspiel.GameType.Dynamics.SEQUENTIAL,
    chance_mode=pyspiel.GameType.ChanceMode.EXPLICIT_STOCHASTIC,
    information=pyspiel.GameType.Information.IMPERFECT_INFORMATION,
    utility=pyspiel.GameType.Utility.ZERO_SUM,
    reward_model=pyspiel.GameType.RewardModel.TERMINAL,
    max_num_players=_NUM_PLAYERS,
    min_num_players=_NUM_PLAYERS,
    provides_information_state_string=True,
    provides_information_state_tensor=True,
    provides_observation_string=True,
    provides_observation_tensor=True)

_GAME_INFO = pyspiel.GameInfo(
    num_distinct_actions=_DISTINCT_ACTIONS,
    max_chance_outcomes=_DECK_SIZE,
    num_players=_NUM_PLAYERS,
    min_utility=-1.0,
    max_utility=1.0,
    utility_sum=0.0,
    max_game_length=_GAME_LENGTH)


_SUITS_STR = ["♥", "♦", "♣", "♠", "§"]
_CARDS_STR = [str(i) for i in range(1, 11)] + ["J", "C", "Q", "K"]
_TRUMP_STR = [str(i) for i in range(0, 22)]
_CARD_VALUES = {
    # Suits
    Rank.JACK: 1.5,
    Rank.KNIGHT: 2.5,
    Rank.QUEEN: 3.5,
    Rank.KING: 4.5,
    # Trumps
    Trump.FOOL: 4.5,
    Trump.PETIT: 4.5,
    Trump.MONDE: 4.5,
}
_SUIT_CARDS = 56
_FOOL = Card(_SUIT_CARDS + Trump.FOOL)
_PETIT = Card(_SUIT_CARDS + Trump.PETIT)


class FrenchTarotGame(pyspiel.Game):
    def __init__(self, params=None):
        super().__init__(_GAME_TYPE, _GAME_INFO, params or dict())
        self._DEFAULT_OBS_TYPE = pyspiel.IIGObservationType(
            perfect_recall=True)

    def new_initial_state(self):
        return FrenchTarotState(self)

    def make_py_observer(self, iig_obs_type=None, params=None):
        return FrenchTarotObserver(
            iig_obs_type or self._DEFAULT_OBS_TYPE,
            params)


class FrenchTarotState(pyspiel.State):
    players: List[Player]
    taker: Player
    dog: Player
    tricks: List[Trick]
    trick: Trick

    _current: Player
    _discard: List[Card]
    _history: List[Tuple[int]]
    _phase: Phase
    _deck: List[Card]
    _fool_paid: bool
    _fool_trick: Trick | None
    _fool_player: Player | None
    _petit_au_bout: int

    def __init__(self, game):
        super().__init__(game)
        self.reset()

    def reset(self):
        self.players = [Player(i) for i in range(_NUM_PLAYERS)]
        self.dog = Player(_DOG_ID, name="Dog")
        self.trick = Trick()
        self.tricks = []

        self._phase = Phase.DEAL
        self._current = self.players[0]
        self._deck = [Card(i) for i in _DECK]
        self._history = []
        self._declares = []
        self._discard = []
        self._fool_paid = False
        self._petit_au_bout = 0

    def current_player(self) -> int:
        if self._phase == Phase.TERMINAL:
            return pyspiel.PlayerId.TERMINAL
        elif self._phase == Phase.PLAY:
            return self._current.id
        return pyspiel.PlayerId.CHANCE

    def _next_player(self) -> Player:
        cards_dealt = len(self._current.hand) % _CARDS_PER_DEAL
        bids = [p.bid for p in self.players if p.bid is not None]
        winner = self.trick.winner()

        if self._phase == Phase.TERMINAL:
            return self._current

        if self._phase == Phase.DEAL:
            if len(self._deck) == 0:
                self._phase = Phase.BID
                return self.players[1]
            if self._current == self.dog:
                self._current = self.players[0]
                return self._current
            if cards_dealt != 0:
                return self._current
            elif self._current == self.players[-1]:
                return self.dog

        if (self._phase == Phase.BID and
                len(bids) == _NUM_PLAYERS):
            self.taker = max(self.players, key=lambda p: p.bid or Bid.PASS)
            if (self.taker.bid == Bid.GUARD_AGAINST or
                    self.taker.bid == Bid.GUARD_WITHOUT):
                self._phase = Phase.DECLARE_SLAM
                return self.taker
            self._phase = Phase.DOG
            self.taker.hand.extend(self.dog.hand)
            self.dog.hand = []
            return self.taker

        if (self._phase == Phase.DOG and
                len(self._discard) == _DOG_SIZE):
            self._phase = Phase.DECLARE_SLAM
            return self.taker

        if (self._phase == Phase.DECLARE_SLAM):
            self._phase = Phase.DECLARE_HANDFUL
            return self.taker

        if (self._phase == Phase.DECLARE_HANDFUL and
                len(self._declares) == _NUM_PLAYERS):
            self._phase = Phase.PLAY
            return self.taker

        if self._phase == Phase.PLAY:
            if (len(self.tricks) == _NUM_TRICKS):
                self._phase = Phase.TERMINAL
                self._settle_petit_au_bout()
                self._settle_fool()
                if not self.dog.hand:
                    self.dog.hand = self._discard
                    self._discard = []
                return self.taker
            if winner:
                self.trick = Trick()
                return winner

        current_index = self.players.index(self._current)
        next_index = (current_index + 1) % _NUM_PLAYERS
        return self.players[next_index]

    def _legal_actions(self, player: int) -> List[int]:
        assert self.current_player() >= 0

        match self._phase:
            case Phase.DEAL:
                return self._legal_actions_deal()
            case Phase.BID:
                return self._legal_actions_bid()
            case Phase.DOG:
                return self._legal_actions_dog()
            case Phase.DECLARE_SLAM:
                return self._legal_actions_slam(player)
            case Phase.DECLARE_HANDFUL:
                return self._legal_actions_handful(player)
            case Phase.PLAY:
                return self._legal_actions_play(player)
        return []

    def _apply_action(self, action: int) -> None:
        match self._phase:
            case Phase.DEAL:
                self._apply_action_deal(Card(action))
            case Phase.BID:
                self._apply_action_bid(Bid(action))
            case Phase.DOG:
                self._apply_action_dog(Card(action))
            case Phase.DECLARE_SLAM:
                self._apply_action_slam(Declaration(action))
            case Phase.DECLARE_HANDFUL:
                self._apply_action_handful(Declaration(action))
            case Phase.PLAY:
                self._apply_action_play(Card(action))
            case Phase.TERMINAL:
                raise ValueError("Invalid game phase")
        self._current = self._next_player()

    def _action_to_string(self, player: int, action: int) -> str:
        assert action >= 0 and action < _DISTINCT_ACTIONS
        if action < _DECK_SIZE:
            return f"[{self._current.name}, {Card(action)}]"
        if action in _BIDS:
            return f"[{self._current.name}, {Bid(action)}]"
        if action in _DECLARATIONS:
            return f"[{self._current.name}, {Declaration(action)}]"
        return "Unknown"

    def chance_outcomes(self) -> List[Tuple[int, float]]:
        assert self.current_player() == pyspiel.PlayerId.CHANCE
        match self._phase:
            case Phase.DEAL:
                return self._chance_deal_actions()
            case Phase.BID:
                return self._chance_bid_actions()
            case Phase.DOG:
                return self._chance_dog_actions()
            case Phase.DECLARE_SLAM:
                return self._chance_slam_actions()
            case Phase.DECLARE_HANDFUL:
                return self._chance_handful_actions()
        return []

    def is_terminal(self) -> bool:
        return self._phase == Phase.TERMINAL

    # ===============
    # Scoring
    # ===============

    def returns(self) -> List[float]:
        taker_points = self._total_score() * (_NUM_PLAYERS - 1)
        defenders_points = -self._total_score()
        results: List[float] = []
        for player in self.players:
            if player == self.taker:
                results.append(taker_points)
            else:
                results.append(defenders_points)
        return results

    def _base_score(self, full_information: bool) -> Tuple[float, bool]:
        assert self.taker.bid is not None
        pile = [c for trick in self.tricks for _,
                c in trick.plays if trick.winner() == self.taker]
        points = sum(c.value for c in pile)
        bouts = sum(1 for c in pile if c.id in [
                    Trump.FOOL, Trump.PETIT, Trump.MONDE])
        required_points = _POINTS_REQUIRED_PER_BOUTS[bouts]

        bid = self.taker.bid
        should_count_dog = (
            (bid != Bid.GUARD_WITHOUT and bid != Bid.GUARD_AGAINST) or
            (bid == Bid.GUARD_WITHOUT and full_information)
        )

        if (should_count_dog):
            points += sum([card.value for card in
                           [*self._discard, *self.dog.hand]])
        required = True if (points - required_points) >= 0 else False
        score = 25 + abs(points - required_points)
        return score, required

    def _total_score(self) -> float:
        assert self.taker.bid is not None
        assert self._phase == Phase.TERMINAL
        points, required = self._base_score(True)
        handful_bonus = 0
        if self.taker.handful == Declaration.DECLARE_HANDFUL:
            trumps = sum(1 for card in self.taker.hand
                         if card.suit == Suit.TRUMPS)
            handful = 0
            for _, threshold in enumerate(_HANDFUL_THRESHOLD):
                if trumps >= threshold:
                    handful += 1
            handful_bonus += _HANDFUL_BONUS[handful]
        slam_bonus = 0
        tricks_won = len(
            [1 for trick in self.tricks if trick._winner == self.taker]) == _NUM_TRICKS
        if tricks_won == _NUM_TRICKS:
            if self.taker.slam == Declaration.DECLARE_SLAM:
                slam_bonus += 400
            else:
                slam_bonus += 200
        elif self.taker.slam == Declaration.DECLARE_SLAM:
            if required:
                slam_bonus -= 400
            else:
                slam_bonus = 400
        points += self._petit_au_bout
        required = 1 if required else -1
        score = required * (points * _BID_MULTIPLIERS[self.taker.bid] +
                            handful_bonus + slam_bonus)
        return score

    # ===============
    # Legal Actions
    # ===============

    def _legal_actions_bid(self) -> List[int]:
        """Returns a list of legal actions for the bidding phase."""
        legal_bids = _BIDS
        bids = [player.bid for player in self.players if player.bid is not None]
        max_bid = max(bids) if bids else Bid.PASS
        legal_bids = [bid for bid in legal_bids if bid > max_bid]
        if Bid.PASS not in legal_bids:
            legal_bids.append(Bid.PASS)
        return [int(i) for i in legal_bids]

    def _legal_actions_deal(self) -> List[int]:
        """Returns a list of legal actions for the dealing phase."""
        return [card.id for card in self._deck]

    def _legal_actions_dog(self) -> List[int]:
        """Returns a list of legal actions for the dog phase."""
        return [card.id for card in self.taker.hand
                if card.suit != Suit.TRUMPS and
                card.rank != Rank.KING]

    def _legal_actions_slam(self, player: int) -> List[int]:
        if self.players[player] == self.taker:
            return [Declaration.DECLARE_NO_SLAM, Declaration.DECLARE_SLAM]
        return [Declaration.DECLARE_NO_SLAM]

    def _legal_actions_handful(self, player: int) -> List[int]:
        trumps = sum(1 for card in self.players[player].hand
                     if card.suit == Suit.TRUMPS)
        if trumps >= _HANDFUL_THRESHOLD[0]:
            return [Declaration.DECLARE_NO_HANDFUL, Declaration.DECLARE_HANDFUL]
        return [Declaration.DECLARE_NO_HANDFUL]

    # ===============
    # Apply Actions
    # ===============
    def _apply_action_bid(self, bid: Bid) -> None:
        bids = sum(1 for p in self.players if p.bid != None)
        assert self._phase == Phase.BID
        assert bids < _NUM_PLAYERS
        self._current.bid = bid

    def _apply_action_deal(self, card: Card) -> None:
        assert self._phase == Phase.DEAL
        assert self._deck
        assert card in self._deck
        self._current.hand.append(card)
        self._deck.remove(card)

    def _apply_action_dog(self, card: Card) -> None:
        assert self._phase == Phase.DOG
        assert len(self._discard) < _DOG_SIZE
        assert card in self.taker.hand
        self.taker.hand.remove(card)
        self._discard.append(card)

    def _apply_action_slam(self, action: Declaration) -> None:
        assert self._phase == Phase.DECLARE_SLAM
        self._current.slam = action

    def _apply_action_handful(self, declare: Declaration) -> None:
        assert self._phase == Phase.DECLARE_HANDFUL
        self._current.handful = declare
        self._declares.append(declare)

    # ===============
    # Chance Actions
    # ===============

    def _chance_deal_actions(self) -> List[Tuple[int, float]]:
        return [(card.id, 1.0 / len(self._deck)) for card in self._deck]

    def _chance_bid_actions(self) -> List[Tuple[int, float]]:
        bids = self._legal_actions_bid()
        return [(bid, 1.0 / len(bids)) for bid in bids]

    def _chance_dog_actions(self) -> List[Tuple[int, float]]:
        legal_discards = self._legal_actions_dog()
        return [(discard, 1.0 / len(legal_discards)) for discard in legal_discards]

    def _chance_slam_actions(self) -> List[Tuple[int, float]]:
        declarations = self._legal_actions_slam(self._current.id)
        return [(declaration, 1.0 / len(declarations)) for declaration in declarations]

    def _chance_handful_actions(self) -> List[Tuple[int, float]]:
        declarations = self._legal_actions_handful(self._current.id)
        return [(declaration, 1.0 / len(declarations)) for declaration in declarations]

    # ===============
    # Play Actions
    # ===============

    def _legal_actions_play(self, player_idx: int) -> List[int]:
        player = self.players[player_idx]
        follow_suit = self.trick.suit
        legal_cards = [c for c in player.hand if c.suit == follow_suit or
                       self.trick.empty()]
        if follow_suit == Suit.TRUMPS and self.trick.rank is not None:
            legal_cards = [c for c in legal_cards if c.rank > self.trick.rank]
        if len(legal_cards) == 0:
            legal_cards = [c for c in player.hand if c.suit == Suit.TRUMPS]
        if len(legal_cards) == 0:
            legal_cards = player.hand

        if (_FOOL in player.hand and
                _FOOL not in legal_cards):
            legal_cards.append(_FOOL)
        return [card.id for card in legal_cards]

    def _apply_action_play(self, card: Card) -> None:
        assert self._phase == Phase.PLAY
        assert card in self._current.hand
        self.trick.append(self._current, card)
        self._current.hand.remove(card)
        winner = self.trick.winner()
        if winner is not None:
            self._current = winner
            self.tricks.append(self.trick)
            winner.tricks.append(self.trick)
        if card == _FOOL:
            self._fool_trick = self.trick
            self._fool_player = self._current

    # ===============
    # Util Functions
    # ===============

    def _settle_fool(self) -> None:
        assert self._phase == Phase.TERMINAL
        assert self._fool_paid is False
        assert self._fool_trick is not None
        assert self._fool_player is not None

        _trick = Trick()
        _trick.append(self._fool_player, _FOOL)
        _trick.winner(self._fool_player)

        self._fool_player.tricks.append(_trick)
        replacement = self._find_replacement(
            self._fool_trick, self._fool_player)
        self._fool_trick.replace(self._fool_player, replacement)
        self._fool_paid = True

    def _settle_petit_au_bout(self) -> None:
        assert self._phase == Phase.TERMINAL
        last_trick = self.tricks[-1]
        if self.tricks and last_trick and _PETIT in [card for _, card in last_trick.plays]:
            taker_won_trick = last_trick.winner() == self.taker
            _, taker_won = self._base_score(False)
            if taker_won_trick and taker_won:
                self._petit_au_bout = 10
            elif not taker_won_trick and taker_won:
                self._petit_au_bout = -10
            elif not taker_won_trick and not taker_won:
                self._petit_au_bout = 10
            else:
                self._petit_au_bout = 0

    def _find_replacement(self, trick: Trick, fool: Player) -> Card:
        winner = trick.winner()
        assert winner is not None
        fool_tricks = [(_trick, card) for _trick in fool.tricks
                       for _player, card in _trick.plays if _player == fool and
                       card.value == 0.5 and trick != _trick]
        if fool_tricks:
            _trick, card = fool_tricks[0]
            _trick.remove(fool, card)
            return card
        raise ValueError("No replacement card found for the fool")

    def __str__(self) -> str:
        string = ""
        _players = self.players + [self.dog]
        for player in _players:
            string += f"{str(player)}\n"
            string += f"\t{f"{'\n\t'.join(map(str, player.tricks) if player.tricks else "")}"}\n"
        string += "\nResults:\n"
        results = self.returns()
        for player_idx, player in enumerate(self.players):
            string += f"{player.name}: {results[player_idx]}\n"
        return string


class FrenchTarotObserver(pyspiel.Observer):
    def __init__(self, iig_obs_type, params):
        """Initializes an empty observation tensor."""
        if params:
            raise ValueError(
                f"Observation parameters not supported; passed {params}")
        self.size = _TENSOR_SIZE
        self.tensor = np.zeros(self.size, np.float32)
        self.dict = {}

        if iig_obs_type.private_info == pyspiel.PrivateInfoType.SINGLE_PLAYER:
            pass
        if iig_obs_type.public_info:
            if iig_obs_type.perfect_recall:
                pass
            else:
                pass

    def set_from(self, state, player):
        pass

    def string_from(self, state, player):
        pass


pyspiel.register_game(_GAME_TYPE, FrenchTarotGame)


def main(_):
    game = pyspiel.load_game(_GAME_NAME)
    state = game.new_initial_state()
    while not state.is_terminal():
        actions = state.legal_actions()
        player = state.current_player()
        if player == pyspiel.PlayerId.CHANCE:
            outcomes, prob = zip(*state.chance_outcomes())
            action = np.random.choice(outcomes, p=prob)
        else:
            action = np.random.choice(actions)
        print(
            f"Action: {state.action_to_string(action)}")
        state.apply_action(action)
    print("=" * 30)
    print(state)


if __name__ == "__main__":
    app.run(main)
