# Chordtroller Omni — webb

Omnichord-motorn på en pekskärm, för att prova på en iPhone i Safari, som en
liten groovebox: Omni, Juno, 303, trummor och en mixer, var och en under sin
egen flik, på samma klocka. En fil, inget byggsteg: `index.html`.

Kör lokalt med valfri statisk server, till exempel `python3 -m http.server` i den
här mappen. Öppna inte filen direkt från disk, för då vägrar webbläsaren ladda
AudioWorkleten och ljudet faller tillbaka på huvudtråden.

## Groovebox

- **Flikarna** uppe till vänster — OMNI, JUNO, 303, TRUMMOR och MIX — byter bara
  vad som ligger framme. Det som spelar fortsätter spela, så man kan lägga en
  trumslinga, gå till 303:an och skruva, och sedan spela ackord på Omnin.
- **PLAY** är gemensam för allt: trummorna, 303:an, Omnins komp och Junons loop
  startar tillsammans på ettan i första takten och stannar tillsammans. Tempot
  (40–220 BPM i steg om 4) delas också, och lampan bredvid blinkar på varje slag
  och lyser gult på ettan.
- Omni och Juno har var sitt ackord. Ett ackord som spelas på Omni-fliken
  ändrar inte Junons, och tvärtom.

## Spela

- **Ackordknapparna** ligger som på en OM-84: tolv grundtoner i kvintordning,
  Eb till G#, och raderna dur, moll och 7. Håller du två knappar i samma kolumn
  blir det dur+moll = dim, dur+7 = maj7 och moll+7 = m7. Ackordet ligger kvar
  när du släpper.
- **Ackordljudet** låter så länge en ackordknapp hålls nere, som Omnichordens
  ackorddel i manuellt läge: Chordtrollers DRONE-figur ur `CompEngine.h`, med
  `CompVoice` som röst — en liten orgel av tre sinusar, en sluten stämföring
  strax under plattans mitt och grundtonen som bas. Byter du ackord med fingret
  kvar stäms den om utan nytt anslag.
- **Kompet** går när PLAY är på. Sex figurer ur `CompEngine.h` — ROCK, VAMP,
  MARSCH, BOSSA, SWING och VALS, i menyn bredvid ljudet — spelar ackordstötar och
  en autobas som växlar mellan grundton och kvint, på det senast tryckta
  ackordet. Ackordet behöver inte hållas medan kompet går. Figurernas trummor är
  borttagna; trumfliken står för trummorna.
- **ACKORDLJUD** och **AUTOBAS** slår av ackorddelen och basen var för sig, i
  både manuellt läge och kompet.
- Hela kompet går förbi chorusen precis som i Chordtroller och möter
  strängarna i rummet. Chorusen och rummet regleras med sina mixrattar i synt
  edit.
- **Strumplattan** har tretton strängar, de lägsta längst ner. Ett finger som
  sätts ner plockar strängen under sig, och ett finger som dras plockar varje
  sträng det passerar. Flera fingrar går bra.
- **Ljudet** väljs i menyn längst till vänster på andra raden: OMNI, HARPA,
  VIBRAFON och GITARR ur `VoiceSpec.h`. Vibrafonen ringer längst och gitarren
  kortast; ett byte kortar eller förlänger också det som redan klingar.
- **Sidan spelas liggande.** Tolv ackordkolumner, sexton steg och en platta bredvid
  knapparna behöver bredden, och en Omnichord hålls också på tvären. En telefon som
  hålls upprätt får en skärm som ber den vridas; webben kan inte vrida den själv.
  På en dator visas alltid instrumentet.
- Plattan till höger går hela höjden. Kontrollerna sitter i två rader över
  knapparna till vänster, likadant för alla flikar: först flikarna, PLAY och
  tempot, sedan EDIT, vad som spelar och instrumentets inställningar.

## Juno

JUNO-fliken är Chordtrollers ackordgenerator på sex Juno-röster.

- **Ackordknapparna** läses som funktioner, som när ringen lämnar över sin
  septimbank: dur, moll, 7 (dominant) och dur+moll (halvminskad). Genren
  bestämmer vilken färg funktionen får — POP, SYNTHWAVE, HOUSE, NEO SOUL, GOSPEL
  eller LOFI ur `JunoChords.h` — så Dm i neo soul blir Dm9. dur+7 och moll+7
  sträcker sig efter genrens utökade färg, som LB gör, och **UTÖKAD** gör det för
  alla knappar. Stämföringen väljer den omvändning som flyttar minst.
- **Plattan** är fraserna ur `JunoPhrases.h`: åtta stilar uppifrån och ner —
  PULS, ARP 1/8, ARP 1/16, RYTM, SYNKOP, STRUM, GATE, FRAS — och fyra variationer
  i sidled, med stilens namn i en egen kolumn. Håll ett finger på en ruta så spelar den, dra till en annan så byts
  stilen på samma slag, och släpp så tystnar den. **HÅLL** låter frasen spela
  vidare efter släpp, som högerspakens klick.
- **CUTOFF** och **RESONANS** under plattan är triggrarnas filter, med samma
  kurvor som `filterSweep`: cutoff rakt, resonansen avsmalnad och stoppad strax
  under självsvängning. Patchens egen envelop ligger ovanpå, så ett plock
  hörs även med cutoff i botten.
- **Ljudet** är patcharna PAD, STRÅKAR, STÖT, PLOCK och BRASS, med var sin
  delay ur `TempoDelay.h`. **BAS** lägger grundtonen under frasen. Chorusen,
  med lägena av, I, II och I+II, ligger i synt edit.
- Frasen går på den gemensamma klockan när PLAY är på, och på en egen när den
  står.

### Spela in

Raden över plattan spelar in det man gör på Junon — ackordknapparna och
plattan — som en loop.

- **REC** startar PLAY om den inte redan går, blinkar till nästa takt och
  spelar sedan in i en, två eller fyra takter (**1 2 4**). Det som klingar när
  inspelningen börjar ligger på första steget. Allt läggs på närmaste
  sextondel. Ett tryck på REC under inspelningen avslutar den tidigare.
- När takterna är slut spelar loopen upp det som spelades, om och om igen,
  medan man går till andra flikar. Ackordknapparna och plattan följer med.
- Ett finger på plattan går före loopen så länge det ligger kvar.
- **LOOP** slår av och på uppspelningen, och **RADERA** tar bort loopen; den
  vill ha två tryck. Loopen sparas i webbläsaren.

## 303

303-fliken är Chordtrollers 303, med sekvensern och rösten ur `AcidEngine.h`
och `AcidVoice.h`. Den spelar när PLAY är på.

- **Rutnätet** till vänster är mönstret: sexton steg i sidled och skalans toner
  från grundtonen upp till oktaven. Tryck i en ruta för att sätta tonen, tryck på
  samma ton igen för paus. **UPP** lyfter steget en oktav, som 303:ans egna
  oktavknappar, **ACC** ger accent och **SLD** glider in i nästa ton. Ett tryck
  på ett stegnummer gör det till loopens sista steg; stegen efter finns kvar,
  dämpade. När sekvensern står hörs varje ton som sätts.
- **Två XY-pads** till höger är rattarna man spelar en 303 med: den första har
  cutoff i sidled och resonans på höjden, den andra env mod i sidled och decay på
  höjden. Värdena ligger kvar när fingret lyfts, och de två går att spela med var
  sin tumme. Under dem sitter delayns mix.
- **Mönster, ljud, skala och tonart** väljs i menyerna. Skalorna är 303:ans:
  moll, frygisk, mollpentatonisk, blues, dur och kromatisk. Tonerna sparas som
  skalsteg, så skala och tonart flyttar hela linjen. **SLUMPA** gör en ny linje
  på samma sätt som i Chordtroller: den lutar mot grundtonen och oktaven, pausar
  ibland, glider bara in i toner och börjar på grundtonen. **RENSA** vill ha två
  tryck. Mönstret ACID är basgången ur Rise of the Arpeggiators ACID-låt.
- **Synt edit** för 303:an har vågform, glide, nivå, cutoff, resonans, env mod,
  decay, delayns mix, eko och tid, oktav och mönstren. **Pump från kicken** är
  hur mycket trumfliken kick trycker ner 303:an, som sidechainen i Rise of the
  Arpeggiator.
  **Spara mönster som** sparar mönstret under ett eget namn. Ljuden sparas som
  för de andra instrumenten.
- 303:an går förbi rummet. En 303 är torr, och delayn står för rymden.

## Trummor

TRUMMOR-fliken är PumpKit ur Rise of the Arpeggiator (`Source/audio/Drums.h`),
stämd som spelets ACID-låt, med en stegsekvenser på åtta spår.

- **Rutnätet** har ett spår per ljud — KICK, CLAP, VIRVEL, HIHAT, ÖPPEN HAT,
  SHAKER, PERC och CRASH — och sexton steg. Ett tryck i en ruta ger slag, nästa
  accent och nästa tyst. Drar man vidare från rutan får de rutor fingret går
  över samma värde, så en rad hihat är ett drag. När PLAY står hörs varje slag
  som sätts.
- **Höger sida** är ljudet för spåret som rördes senast: en stor platta som
  spelar det, och **STÄMNING** (en oktav ner till en oktav upp, i halvtoner),
  **LÄNGD**, **NIVÅ** och **PANORERING**. Spårnamnet i rutnätet väljer och
  spelar spåret. Ett dubbeltryck på ett reglage ställer tillbaka det.
- Den öppna hihaten tystnar när den slutna slår, som på en 808.
- **Mönstren** HOUSE, ACID, BREAK och TOM väljs i menyn. HOUSE är alla spelets
  lager på en gång. **SLUMPA** gör ett nytt mönster på det aktiva: kick på
  fyra med en knuff ibland, backbeat på clap eller virvel, hihat i en av tre
  tätheter och resten strött. **RENSA** vill ha två tryck.
- Trummorna går förbi rummet.

## Mix

MIX-fliken har en kanal per instrument: OMNI, JUNO, 303 och TRUMMOR.

- **MUTE** tystar kanalen, och fliken stryks över så att det syns även från de
  andra flikarna.
- **Volymen** dras upp och ner. Strecket vid 80 % är 0 dB, och toppen är +8 dB.
- **Filtret** under är ett DJ-filter: till vänster om mitten ett lågpass som
  stänger mot 35 Hz, till höger ett högpass som öppnar mot 14 kHz, med en
  resonans som växer ju längre det går. Mitten är av, med en liten död zon.
- Ett dubbeltryck ställer tillbaka volymen eller filtret. **NOLLSTÄLL MIXEN**
  ställer tillbaka alla kanaler.

## Synt edit

Alla Omni-ljud är samma strängröst med olika rader ur `VoiceSpec.h`, och alla
Juno-ljud är rader ur patchtabellen, så att redigera ett ljud är att ändra dess
rad. **EDIT** i sidhuvudet byter vänstra halvan till redigering av ljudet som är
valt; plattan till höger ligger kvar, så det går att spela medan man rattar, och
**KLAR** går tillbaka.

- **Omni:** vågform, pulsbredd, detune, ljushet (filtret som multipel av tonen),
  attack, ringtid och nivå. Effekter: chorusens hastighet och mix.
- **Juno:** såg, puls, sub, brus, pulsbredd, PWM och LFO; cutoff, resonans,
  envelope-mängd, key track och HPF; attack, decay, sustain, release och nivå.
  Effekter: delayns mängd, återkoppling och tid, som hör till patchen precis
  som i Chordtroller, och chorusläget.
- **Juno-LFO:** form (sinus, triangel, fyrkant, sample & hold), hastighet,
  fördröjning (vibrato och filtersvep tonas in efter anslaget), tonhöjd i
  halvtoner, filter och PWM. Fabriksljuden har allt utom PWM på noll och låter
  som förut.
- **Rummet** — storlek, dämpning och mix — delas av båda instrumenten.

**Spara som** sparar ljudet som det låter just nu under ett eget namn. Egna ljud
hamnar under *Egna* i ljudmenyn, efter fabriksljuden, och ändringar i dem sparas
direkt. **Radera ljud** tar bort ett eget ljud; den frågar genom att vilja ha ett
andra tryck inom tre sekunder. Effekterna hör inte till ljudet, utom Junons
delay, som hör till patchen som i Chordtroller.

Reglagen i listan flyttas från där de står: dra i sidled för att ändra, upp
och ner för att rulla. **Återställ ljud** och **Återställ effekter** längst ner
tar tillbaka originalvärdena. Ändringarna sparas i webbläsaren.

## Vad som är portat och vad som är nytt

Portat rakt av, med kommentarerna kvar:

- `PluckVoice.h` — strängrösten, OMNI-klangen ur `VoiceSpec.h`
- `Ensemble.h` — chorusen
- `CompVoice.h` och figurerna och stämföringen ur `CompEngine.h` —
  ackordljudet, kompet och autobasen, utan figurernas trummor
- `JunoVoice.h`, `JunoEngine.h`, `JunoPhrases.h`, `JunoChords.h` och
  `TempoDelay.h` — Juno, utan MIDI-ut
- `AcidVoice.h` och `AcidEngine.h` — 303:an, utan MIDI-ut och värdsynk
- PumpKit ur Rise of the Arpeggiators `Source/audio/Drums.h` — trumfliken
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

Nytt för grooveboxen: den gemensamma klockan, trumsekvensern, mixern med
DJ-filtret och Junons loop.

Ringlängden är fast på fyra sekunder (LT släppt) och volymen på full (RT inne).

## iPhone

- Ljudet startar vid första tryckningen. iOS räknar en beröring som en
  användargest först när fingret lyfts, så första tonen kan utebli.
- `navigator.audioSession.type = "playback"` sätts där den finns (Safari 17+),
  så att ljudet hörs även med ljudlöst-knappen på.
- Lägg sidan på hemskärmen för helskärm utan Safaris verktygsfält.
