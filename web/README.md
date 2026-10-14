# Chordtroller Omni — webb

Omnichord-motorn på en pekskärm, för att prova på en iPhone i Safari. En fil,
inget byggsteg: `index.html`.

Kör lokalt med valfri statisk server, till exempel `python3 -m http.server` i den
här mappen. Öppna inte filen direkt från disk, för då vägrar webbläsaren ladda
AudioWorkleten och ljudet faller tillbaka på huvudtråden.

## Spela

- **Ackordknapparna** ligger som på en OM-84: tolv grundtoner i kvintordning,
  Eb till G#, och raderna dur, moll och 7. Håller du två knappar i samma kolumn
  blir det dur+moll = dim, dur+7 = maj7 och moll+7 = m7. Ackordet ligger kvar
  när du släpper.
- **Ackordljudet** låter så länge en ackordknapp hålls nere, som Omnichordens
  ackorddel: Chordtrollers DRONE-figur ur `CompEngine.h`, med `CompVoice` som
  röst — en liten orgel av tre sinusar, en sluten stämföring strax under
  plattans mitt och grundtonen som bas. Den går förbi chorusen precis som kompet
  gör och möter strängarna i rummet. Byter du ackord med fingret kvar stäms den
  om utan nytt anslag. Knappen ACKORDLJUD slår av och på den.
- **Strumplattan** har tretton strängar, de lägsta längst ner. Ett finger som
  sätts ner plockar strängen under sig, och ett finger som dras plockar varje
  sträng det passerar. Flera fingrar går bra.
- Liggande läge ger tolv kolumner och tre rader. Stående läge vrider gridet till
  tolv rader och tre kolumner.

## Vad som är portat och vad som är nytt

Portat rakt av, med kommentarerna kvar:

- `PluckVoice.h` — strängrösten, OMNI-klangen ur `VoiceSpec.h`
- `Ensemble.h` — chorusen
- `CompVoice.h` och DRONE-stämföringen ur `CompEngine.h` — ackordljudet
- `StringSet` i `ChordRing.h` — plattans tonordning, så svepet rullar som
  originalet: C3 E3 G2 C4 E4 G3 …
- rummet är Freeverb med samma parametrar som `juce::Reverb` har i processorn
- rösttilldelningen ur `StrumEngine` — en ringande sträng tar tillbaka sin egen ton

Nytt, eftersom en skärm inte är en spak: `StrumEngine` mappar spakens utslag
till en plockhastighet. På en platta är det läget som spelar, så fingret plockar
strängen det står på, och en liten hysteres på strängkanterna gör att ett finger
som vilar på en linje inte fladdrar. Strängar som passeras inom ett och samma
pekhändelse sprids ut över den tid händelsen täckte, och ett snabbt svep plockar
lättare som i `strumVelocity`.

Ringlängden är fast på fyra sekunder (LT släppt) och volymen på full (RT inne).

## iPhone

- Ljudet startar vid första tryckningen. iOS räknar en beröring som en
  användargest först när fingret lyfts, så första tonen kan utebli.
- `navigator.audioSession.type = "playback"` sätts där den finns (Safari 17+),
  så att ljudet hörs även med ljudlöst-knappen på.
- Lägg sidan på hemskärmen för helskärm utan Safaris verktygsfält.
