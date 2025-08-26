
from dataclasses import dataclass
import numpy as np  # type: ignore
import pyspiel  # type: ignore
import enum
from typing import List, Optional, Set, Tuple
from collections.abc import Sequence


class Action(enum.IntEnum):
    # 0-77 are reserved for cards
    # The deck has 4 suits: hearts, diamonds, clubs, and spades
    # Each suit has 14 cards: 1-10, Jack, Knight, Queen, King
    # The deck has 22 trump cards, named 0-21
    # Trump #0 is called Fou (Fool), #1 is called Petit (Small),
    # #21 is called Monde (World)

    FOOL = 56
    PETIT = 57
    MONDE = 77

    # Bid Actions
    PASS = 78
    SMALL = 79
    GUARD = 80
    GUARD_WITHOUT = 81
    GUARD_AGAINST = 82

    # Declare Actions
    DECLARE_NO_HANDFUL = 83
    DECLARE_NO_SLAM = 84
    DECLARE_HANDFUL = 85
    DECLARE_SLAM = 86


class Phase(enum.IntEnum):
    DEAL = 0  # Dealing phase
    BID = 1  # Contract phase
    DOG = 2  # Dog (chien) phase
    DECLARE_SLAM = 3  # Declare slam (chelem) phase
    DECLARE_HANDFUL = 4  # Declare handful (poignee) phase
    PLAY = 5  # Trick-Taking phase
    TERMINAL = 6  # Game over


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

# ===============
# GAME ACTIONS
# ===============
_BIDS = frozenset([Action.PASS, Action.SMALL, Action.GUARD,
                  Action.GUARD_WITHOUT, Action.GUARD_AGAINST])
_DECLARATIONS = frozenset([Action.DECLARE_NO_HANDFUL, Action.DECLARE_NO_SLAM,
                           Action.DECLARE_HANDFUL, Action.DECLARE_SLAM])

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

_GAME_TYPE = pyspiel.GameType(
    short_name="french_tarot",
    long_name="French Tarot",
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

_DEFAULT_OBS_TYPE = pyspiel.IIGObservationType(perfect_recall=True)


class Suit(enum.IntEnum):
    HEARTS = 0
    DIAMONDS = 1
    CLUBS = 2
    SPADES = 3
    TRUMPS = 4


class Rank(enum.IntEnum):
    JACK = 10
    KNIGHT = 11
    QUEEN = 12
    KING = 13


_SUITS_STR = ["♥", "♦", "♣", "♠", "◘"]
_CARDS_STR = ["A"] + [str(i) for i in range(2, 11)] + ["J", "C", "Q", "K"]


class Card:
    id: int
    rank: Rank | int
    suit: Suit

    def __init__(self, id: int) -> None:
        self.id = id
        try:
            self.rank = Rank(id % _CARDS_PER_SUIT)
        except ValueError:
            self.rank = id % _CARDS_PER_SUIT
        self.suit = Suit(id // _CARDS_PER_SUIT)

    def __str__(self) -> str:
        return f"{_SUITS_STR[self.suit]}{_CARDS_STR[self.rank]}"


_FOOL = Card(Action.FOOL)
_PETIT = Card(Action.PETIT)
_MONDE = Card(Action.MONDE)


@dataclass
class Player:
    id: int = -1
    bid: Action | None = None
    slam: Action | None = None
    handful: Action | None = None
    hand: List[Card] = []


@dataclass
class Trick:
    cards: List[Card] = []
    lead_player: Player | None = None
    lead_suit: Suit | None = None
    lead_rank: Rank | int | None = None
    _winner: Player | None = None

    def append(self, card: Card, player: Player):
        suits_available = set(c.suit for c in player.hand)
        ranks_available = set(c.rank for c in player.hand
                              if c.suit == self.lead_suit)
        max_rank = max(ranks_available) if ranks_available else 0
        if (self.lead_player is None or
                self.lead_suit is None or
                self.lead_rank is None):
            self.lead_player = player
            self.lead_suit = card.suit
            self.lead_rank = card.rank
        has_suit = (self.lead_suit in suits_available)
        has_suit_and_greater = (card.suit == self.lead_suit and
                                (card.rank > self.lead_rank or
                                 max_rank < self.lead_rank))
        is_follow_trumps = (self.lead_suit != Suit.TRUMPS and
                            card.suit == Suit.TRUMPS)
        assert has_suit_and_greater or is_follow_trumps or not has_suit
        assert len(self.cards) < _NUM_PLAYERS
        if is_follow_trumps:
            self.lead_suit = Suit.TRUMPS
        if has_suit_and_greater:
            self.lead_rank = card.rank
        self.cards.append(card)
        if has_suit_and_greater or (not has_suit and is_follow_trumps):
            self._winner = player

    def get_winner(self) -> Player | None:
        if len(self.cards) != _NUM_PLAYERS:
            return None
        return self._winner

    def __str__(self) -> str:
        return f"Trick[{" ".join(str(card) for card in self.cards)}]"


class FrenchTarotGame(pyspiel.Game):
    def __init__(self):
        super().__init__(_GAME_TYPE, _GAME_INFO)

    def new_initial_state(self):
        return FrenchTarotState(self)

    def make_py_observer(self, iig_obs_type=None, params=None):
        return FrenchTarotObserver(
            iig_obs_type or _DEFAULT_OBS_TYPE,
            params)


class FrenchTarotState(pyspiel.State):
    players: List[Player] = []
    taker: Player
    dog: Player
    tricks: List[Trick] = []
    trick: Trick

    _current: Player
    _discard: List[Card] = []
    _history: List[Tuple[int, Action]] = []
    _phase: Phase
    _deck: Set[Card]

    def __init__(self, game):
        super().__init__(game)
        self.reset()

    def reset(self):
        self.players = [Player(id=i) for i in range(_NUM_PLAYERS)]
        self.dog = Player(_DOG_ID)
        self._phase = Phase.DEAL
        self._current = self.players[0]
        self._deck = set([Card(i) for i in _DECK])
        self._history = []

    def current_player(self) -> int:
        """Returns id of the next player to move, or TERMINAL if game is over."""
        if self._phase == Phase.TERMINAL:
            return pyspiel.PlayerId.TERMINAL
        elif self._phase == Phase.PLAY:
            return self._current.id
        return pyspiel.PlayerId.CHANCE

    def _next_player(self) -> Player:
        cards_dealt = len(self._current.hand) % _CARDS_PER_DEAL

        if self._phase == Phase.TERMINAL:
            return self._current

        if self._phase == Phase.DEAL:
            if len(self._deck) == 0:
                self._phase = Phase.BID
                return self.players[0]
            if cards_dealt != 0:
                return self._current
            elif self._current.id == _NUM_PLAYERS - 1:
                return self.dog

        if (self._phase == Phase.BID and
                len(self.bids) == _NUM_PLAYERS):
            self._phase = Phase.DOG
            self.taker = max(self.players, key=lambda p: p.bid or Action.PASS)
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
                len(self.handful) == _NUM_PLAYERS):
            self._phase = Phase.PLAY
            return self.taker
        
        if (self._phase == Phase.PLAY and
                len(self.tricks) == _NUM_TRICKS):
            self._phase = Phase.TERMINAL
            return self._current

        current_index = self.players.index(self._current)
        next_index = (current_index + 1) % _NUM_PLAYERS
        return self.players[next_index]

    def _legal_actions(self, player: int) -> List[int]:
        """Returns a list of legal actions, sorted in ascending order."""
        assert self.current_player() >= 0
        if self._phase == Phase.DEAL:
            return self._legal_actions_deal()
        elif self._phase == Phase.BID:
            return self._legal_actions_bid()
        elif self._phase == Phase.DOG:
            return self._legal_actions_dog()
        elif self._phase == Phase.DECLARE_SLAM:
            return self._legal_actions_slam(player)
        elif self._phase == Phase.DECLARE_HANDFUL:
            return self._legal_actions_handful(player)
        return []

    def _apply_action(self, action: int) -> None:
        """Applies the specified action to the state."""
        if self._phase == Phase.DEAL:
            self._apply_action_deal(action)
        elif self._phase == Phase.BID:
            self._apply_action_bid(action)
        elif self._phase == Phase.DOG:
            self._apply_action_dog(action)
        elif self._phase == Phase.DECLARE_SLAM:
            self._apply_action_slam(action)
        elif self._phase == Phase.DECLARE_HANDFUL:
            self._apply_action_handful(action)
        elif self._phase != Phase.TERMINAL:
            raise ValueError("Invalid game phase")
        self._current = self._next_player()

    def _action_to_string(self, player: int, action: int) -> str:
        """Action -> string."""
        assert action >= 0 and action < _DISTINCT_ACTIONS
        if action < _DECK_SIZE:
            return f"[P{player}, {Card(action)}]"
        if action in _BIDS or action in _DECLARATIONS:
            return f"[P{player}, {Action(action).name}]"
        return "Unknown"

    def chance_outcomes(self) -> List[Tuple[int, float]]:
        """Returns a list of possible outcomes for the current chance node."""
        assert self.current_player() == pyspiel.PlayerId.CHANCE
        if self._phase == Phase.DEAL:
            return self._chance_deal_actions()
        elif self._phase == Phase.BID:
            return self._chance_bid_actions()
        elif self._phase == Phase.DOG:
            return self._chance_dog_actions()
        return []

    def is_terminal(self) -> bool:
        """Returns True if the game is over."""
        return self._phase == Phase.TERMINAL

    def returns(self) -> List[float]:
        """Total reward for each player over the course of the game so far."""
        return [0.0 for _ in range(_NUM_PLAYERS)]

    # ===============
    # Legal Actions
    # ===============

    def _legal_actions_bid(self) -> List[int]:
        """Returns a list of legal actions for the bidding phase."""
        legal_bids = [Action.PASS, Action.GUARD,
                      Action.GUARD_WITHOUT, Action.GUARD_AGAINST]
        bids = [player.bid for player in self.players if player.bid is not None]
        max_bid = max(bids) if bids else Action.PASS
        legal_bids = [bid for bid in legal_bids if bid > max_bid]
        if Action.PASS not in legal_bids:
            legal_bids.append(Action.PASS)
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
            return [Action.DECLARE_NO_SLAM, Action.DECLARE_SLAM]
        return [Action.DECLARE_NO_SLAM]

    def _legal_actions_handful(self, player: int) -> List[int]:
        trumps = sum(1 for card in self.players[player].hand
                     if card.suit == Suit.TRUMPS)
        if trumps >= _HANDFUL_THRESHOLD[0]:
            return [Action.DECLARE_NO_HANDFUL, Action.DECLARE_HANDFUL]
        return [Action.DECLARE_NO_HANDFUL]

    # ===============
    # Apply Actions
    # ===============
    def _apply_action_bid(self, action: int) -> None:
        bids = sum(1 for p in self.players if p.bid != None)
        assert self._phase == Phase.BID
        assert bids < _NUM_PLAYERS
        self._current.bid = Action(action)

    def _apply_action_deal(self, action: int) -> None:
        assert self._phase == Phase.DEAL
        assert self._deck
        assert action in self._deck
        self._current.hand.append(Card(action))

    def _apply_action_dog(self, action: int) -> None:
        assert self._phase == Phase.DOG
        assert len(self._discard) < _DOG_SIZE
        assert action in self.taker.hand
        self.taker.hand.remove(action)
        self._discard.append(action)

    def _apply_action_slam(self, action: int) -> None:
        assert self._phase == Phase.DECLARE_SLAM
        self._current.slam = Action(action)

    def _apply_action_handful(self, action: int) -> None:
        assert self._phase == Phase.DECLARE_HANDFUL
        self._current.handful = Action(action)

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

    # ===============
    # Play Actions
    # ===============

    def _legal_actions_play(self, player_idx: int) -> List[int]:
        player = self.players[player_idx]
        follow_suit = self.trick.lead_suit
        legal_cards = [c for c in player.hand if c.suit == follow_suit]

        if not legal_cards:
            legal_cards = [c for c in player.hand if c.suit == Suit.TRUMPS]

        if not legal_cards:
            legal_cards = player.hand.copy()

        if (_FOOL in player.hand and
                _FOOL not in legal_cards):
            legal_cards.append(_FOOL)
        return [card.id for card in legal_cards]

    def _apply_action_play(self, action: int) -> None:
        card = Card(action)
        assert self._phase == Phase.PLAY
        assert card in self._current.hand
        self._current.hand.remove(card)
        self.trick.append(card, self._current)
        winner = self.trick.get_winner()
        if winner is not None:
            self._current = winner
            self.tricks.append(self.trick)
            self.trick = Trick()


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
