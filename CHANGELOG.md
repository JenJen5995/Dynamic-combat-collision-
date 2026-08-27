# Changelog

## 1.4.3

- Less hitching in big fights: Don't get pushed and Attack translation no longer lock Havok every frame to measure hulls.
- Combat collision is not reapplied every tick once it is already at the right size (weapon swap, slide, and hull rebuilds still update).
- Player hull log lines are written only when something actually changes, not every frame.
- Turning off Ally fights keeps anyone not hostile to you at vanilla collision, even in the same fight. Followers get a hull again when the option is on.

## 1.4.2

- Fixed freezes from the plugin locking Havok twice during combat.
- Combat size is no longer applied a second time on the physics thread (stops oversized or fighting hulls).
- Spiders and similar enemies no longer get a shared, broken collision shape.
- Fixed a crash when putting collision back after another mod rebuilds the hull.
- Huge fights no longer overwrite the first character’s collision data when the cache is full.
- Fixed the collision ring growing much too large after a slide or sprint.
- Turning off Attack translation now actually stops NPCs from being pulled into you.
- Combat collision is no longer updated twice in the same tick.
- Added **Doorframes** (on by default): keeps fat combat collision out of doors and furniture. Turn it off if interiors hitch; it costs a bit of performance when on.
- Crash-logger PDB is included next to the DLL.
- Downloads are split: SE/AE plugin, VR plugin, and optional MCM.
- Translations ship with the plugin. MCM (ESP + SkyUI page) is optional. SKSE Menu Framework still works with no ESP.
- SKSE Menu Framework settings now save to the game Data folder, and weapon sliders save when you release them.
- Spiders and other untagged automata no longer enter combat scaling.
- Added **NPCs with hull** (default all): 1-10 fattens only the closest NPCs in the fight; 11 is every fight NPC. Cuts cost in big packs. Lock-on only still uses only the lock target.
- Combat tracking is capped and drops unloaded actors so a long session cannot block new hulls.

## 1.4.1

- Wolves, animals, and similar creatures no longer get combat collision (fixes lock-on / pack freezes).
- Lock-on only no longer fattens whatever you lock every frame.
- Fixed a crash when loading a save (Stony Creek and similar).
- Giants and other already-huge bodies are no longer multiplied to absurd sizes.
- Don’t get pushed no longer yeets actors across the map in one frame.
- Less log spam during combat.
