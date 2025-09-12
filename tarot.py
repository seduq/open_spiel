
import open_spiel.python.games
import pyspiel
import numpy as np
from absl import app

def main(_):
    game = pyspiel.load_game("python_french_tarot", {"players": 3})
    state = game.new_initial_state()
    while not state.is_terminal():
        actions = state.legal_actions()
        player = state.current_player()
        if player == pyspiel.PlayerId.CHANCE:
            outcomes, prob = zip(*state.chance_outcomes())
            action = np.random.choice(outcomes, p=prob)
        else:
            action = np.random.choice(actions)
        # print(f"Action: {state.action_to_string(action)}")
        state.apply_action(action)
    print("=" * 30)
    print(state)


if __name__ == "__main__":
    app.run(main)