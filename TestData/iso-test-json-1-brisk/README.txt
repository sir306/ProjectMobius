ISO test JSON B-Risk health scenario
====================================

Load:
  iso-test-json-1-brisk.smv

Matched agent data:
  D:\NickWork\Mobius\ProjectMobius\TestData\iso-test-json-1.json

Hazard room:
  Origin: 0, -15, 0 metres
  Size:   10 x 15 x 2.6 metres

ProjectMobius inverts JSON Y coordinates during agent import, so source JSON
Y values 0..15 become Unreal/B-Risk Y values -15..0.

The room covers the left evacuation stream. Based on the JSON trajectories,
agents 8, 9, 12, 13, 14, 17, 18, 19, 20, 21 and 22 remain in the room for at
least ten seconds. The other twelve agents do not enter the room.

Health calibration for the current UAgentEgressHealthCalculationProcessor:
  UHCN_1/LHCN_1 = 800 ppm
  HCN threshold = 80 ppm
  HCN dose budget = 7200 ppm-seconds

  (800 - 80) / 7200 = 0.10 health loss per exposed simulation second.

ULOD_1 and LLOD_1 are held at 0.1 1/m. This makes the smoke visible while
remaining at the current optical-density health threshold, so it adds no
extra health loss. HGT_1 is 0.5 m, placing every agent breathing height in the
upper layer; both layers contain the same HCN value for robustness.

The visible fire runs from 0 to 10 seconds. Smoke and gas remain in the room
through the 23-second scenario. With current processor defaults, 11 of 23
agents should reach zero health.
