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

#include "open_spiel/algorithms/get_all_states.h"
#include "open_spiel/policy.h"
#include "open_spiel/spiel_utils.h"
#include "open_spiel/tests/basic_tests.h"

namespace open_spiel
{
  namespace french_tarot
  {
    namespace
    {

      namespace testing = open_spiel::testing;

      void BasicFrenchTarotTests()
      {
        testing::LoadGameTest("french_tarot");
        testing::ChanceOutcomesTest(*LoadGame("french_tarot"));
        testing::RandomSimTest(*LoadGame("french_tarot"), 100);
        testing::RandomSimTestWithUndo(*LoadGame("french_tarot"), 1);
        for (Player players = kMinNumPlayers; players <= kMaxNumPlayers; players++)
        {
          testing::RandomSimTest(
              *LoadGame("french_tarot", {{"players", GameParameter(players)}}), 100);
        }
        auto observer = LoadGame("french_tarot")
                            ->MakeObserver(kDefaultObsType,
                                           GameParametersFromString("single_tensor"));
        testing::RandomSimTestCustomObserver(*LoadGame("french_tarot"), observer);
      }

      void BidTests();
      void DealTests();
      void TrickTakingTests();
      void ScoringTests();
    } // namespace
  } // namespace french_tarot
} // namespace open_spiel

int main(int argc, char **argv)
{
  open_spiel::french_tarot::BasicFrenchTarotTests();
  open_spiel::testing::CheckChanceOutcomes(
      *open_spiel::LoadGame("french_tarot", {{"players", open_spiel::GameParameter(3)}}));
  open_spiel::testing::RandomSimTest(
      *open_spiel::LoadGame("french_tarot"), /*num_sims=*/10);
  open_spiel::testing::ResampleInfostateTest(
      *open_spiel::LoadGame("french_tarot"), /*num_sims=*/10);
}
