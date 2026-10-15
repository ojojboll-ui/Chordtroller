# Chordtroller

Ett Omnichord-inspirerat instrument som spelas med en Xbox-handkontroll. VST3 och
standalone, JUCE 8.0.4, C++17.

Projektplanen ligger i `../chordtroller-projektplan.html` och designunderlaget i
`../design_handoff_chordtroller/`.

## Var projektet står

**Fas 1 — indataspik. Klar och verifierad.** 237–240 Hz uppmätt med 7,5 ms
värsta lucka, kvadratnormaliseringen når hela vägen ut i diagonalerna, sektorn
står stilla på en gräns.

**Fas 2 — instrumentet spelar. Byggd.** Plockmotor, ackordring, en strängröst
och volym. Sju beslut togs innan bygget och alla gick på rekommendationen: toner
ringer ut över ackordbyten, enkeltonströskeln är samma dödzon som svepet, bank A
bara, tonart på d-pad, och sustain på LT redan nu.

Tre saker ändrades efteråt. **Dämpningen på högerspakens X är borttagen** — LT
ensam styr ringlängden. **Plockordningen är inte längre stigande**, se nedan,
vilket också ersatte beslutet om fast register: registret följer ackordet, för
originalets mönster är definierat relativt grundtonen. Och **x-axeln väljer nu
oktavzon** — vänster ger en låg figur, höger en hög, mitten hela plattan.

**Fas 3 — ljudkaraktären. Byggd.** Chorus, reverb, fyra röster och vibration.

**Luckorna och MIDI-ut. Byggda.** Hela kontrollkartan är nu inkopplad utom det
som hör till v2: LB håller septimbanken, View och Meny skiftar oktav, LS-klick
låser ackordet. Och varje plockad ton skickas som MIDI med sampeloffset, så
DAW:en kan spela in framförandet.

**Gränssnittet.** Designunderlagets ritade handkontroll byggs inte. Instrumentets
ansikte är `ui/InstrumentPanel`, som är fas 1-mätskärmens formspråk med
felsökningsraderna borttagna. Mätskärmen finns kvar bakom knappen DIAGNOSTIK.

**Kompet, utan trummor. Byggt.** Autobas och ackordkomp i nio figurer på tre
knappar. RB startar och stoppar. A/B/X äger var sin familj om tre och stegar
genom den, så varje knapp betyder fortfarande en sak: **A håller** (DRONE, PAD,
BAS), **B marscherar** (VAMP, MARSCH, ROCK), **X svänger** (BOSSA, SWING, VALS).

Rutnätet är per figur, vilket är det som får swingen att svänga och valsen att
gå runt: SWING kör på ett triolrutnät eftersom en shuffle inte går att uttrycka i
sextondelar, och VALS behåller sextondelar men bara tre slag i takten.

**Trummor. Byggda.** MENY slår på och av dem. Kick, virvel och hihat, gjorda och
inte samplade: en kick är en sinus som faller nedför ett stup, en virvel är brus
med ett stämt duns under, en hihat är brus med botten borttagen. Varje figur har
egna trumslag, och de **håller-figurerna har också trummor** — en drone över en
backbeat är precis vad originalets manual mode plus rytm är.

Kvar: kompets toner i MIDI-ut, och plugin-tillstånd — presets, sparat läge och
arbitrering när flera instanser vill åt samma kontroll.

**Knappbudgeten är slut, och trummorna kostade en knapp.** Fjorton knappar, två
spakklick och två triggrar, alla tilldelade. VIEW stegar därför nu oktaven runt
−2…+2 i stället för att bara sänka, och MENY blev trumknapp. Oktavskiftet ställer
man in mellan låtar; trummor kastar man in mitt i en låt, så de fick knappen man
når utan att släppa någon spak.

**Ergonomin styrde knappvalet.** Face-knapparna kan bara nås genom att släppa
plockspaken, eftersom höger tumme sitter där. Det gör dem fel för allt som ändras
mitt i en fras och rätt för sådant man ställer in mellan låtar — därför bor
dur/moll på Y och kompfigurerna på A/B/X. Start och stopp är däremot något man
gör mitt i spelandet, så det hamnade på RB, som pekfingret når utan att någon
tumme släpper taget. Designens plan att lägga allt kompet på face-knapparna hade
inte gått att spela.

**Håll in RT för att höra något.** Höger trigger är hela volymen, som en
svällpedal, så ett släppt reglage är tystnad och inte ett fel.

## Händer

Chordtroller kan spelas med två händer framför en webbkamera. Programmet
`../handtrack` läser kameran och skickar handens läge och fart som OSC till
`127.0.0.1:9000`; `input/HandSource.cpp` lyssnar där och gör om det till samma
`PadState` som en handkontroll ger, så alla instrument, ljud och MIDI-ut fungerar
oförändrat. Starta handtrack (`python -m handtrack --osc`) och sedan Chordtroller.
Rubriken visar "Hander (kamera)" när strömmen når fram.

| Hand | Funktion | Motsvarar |
|---|---|---|
| Vänster, läge runt hemmet | Ackordring | vänster spak |
| Vänster, nyp | Strängarna dämpas | LT |
| Vänster, knuten | Septimbanken | LB |
| Höger, läge ovanför/under mitten | Strummar av sig själv, uppåt eller nedåt; tyst i mitten | höger spak Y |
| Höger, läge i sidled | Omfång | höger spak X |
| Höger, synlig | Volym | RT |

**Höger hand är höger spak.** Håll den ovanför mitten av bilden så strummar
instrumentet uppåt av sig självt, under mitten så strummar det nedåt, och ju längre
från mitten desto snabbare. I mitten är det tyst. Ett läge står still, så kamerans 30
bilder i sekunden och dess fördröjning spelar ingen roll, vilket de gör för en rörelse
mätt som fart. Det som räknas som mitten är en grå list i handtracks förhandsvisning
längs högerkanten; handen blir grön när den är utanför den och strummen går. Instrumentets
egna döda zoner ligger ovanpå, så listen är ungefär ±6,5 % av bildhöjden. Kurvan ut från mitten är en
logaritmisk taper (`strumTaper` i `HandTuning`, 1,6; 0 är linjär, negativt är snabbt
från start): strummen börjar på 2 toner i sekunden vid listens kant och tar fart
först mot kanten, där den når 24. Spaken är dessutom utjämnad med en tidskonstant på
80 ms (`strumSmoothing`, 0 stänger av), eftersom kameran rapporterar ungefär 30
gånger i sekunden och varje rapport har lite brus; utan den hoppar strumhastigheten
steg för steg.
Taperns startvärde är satt så att strummen börjar precis där listen slutar.
Sidled är omfånget, som högerspaken i sidled. Hemmet är 72 % av bildbredden och 50 % av höjden, spegelvänt.

Det finns två äldre sätt kvar i `HandTuning::strumSource`: `speed` (handens höjdfart) och
`fingers` (fingrarnas stängningshastighet). Ingen av dem följde handen tillräckligt väl
för att spela.

Händerna har lägst prioritet: en Xbox- eller Switch-kontroll som är inkopplad
vinner. Mappningen är fast och ligger i `input/HandMapping.h` (`HandTuning`).
Hemlägena är vänster hand vid 28 % av bildbredden och höger vid 72 %, spegelvänt.
En hand som lämnar bilden släpper alla spakar, och ackordet ligger kvar. Tystnar
strömmen helt i mer än en och en halv sekund räknas källan som bortkopplad.

Lyssnaren binder bara till loopback, och porten byts med miljövariabeln
`CHORDTROLLER_HAND_PORT`. Är porten upptagen, till exempel av en andra instans,
visas "Hander (porten upptagen)" och den försöker igen varje sekund.

Tester: `tools/enginetests/handtest.cpp` kör OSC-läsaren mot paket gjorda av
python-osc och mot avklippta och slumpmässiga byte (under AddressSanitizer), och
mappningen med påhittade händer. `tools/panelshot/hand_check.cpp` skickar OSC över
en riktig UDP-socka genom `HandSource` och processorn och läser MIDI i andra änden.

## Bygga

```
cmake -B build
cmake --build build --config Release
```

Första körningen hämtar JUCE, ungefär 200 MB. Finns redan en JUCE-checkout på
maskinen går det snabbare att peka på den:

```
cmake -B build -DFETCHCONTENT_SOURCE_DIR_JUCE="../scriptor/build/_deps/juce-src"
```

Resultatet hamnar i `build/Chordtroller_artefacts/Release/` — `Standalone/Chordtroller.exe`
och `VST3/Chordtroller.vst3`. Lägg till `-DCHORDTROLLER_INSTALL_PLUGIN=ON` från
en upphöjd prompt för att kopiera VST3:an direkt till systemmappen.

## Vad fas 1 ska bevisa, och hur du kontrollerar det

Starta standalone-appen med en Xbox-kontroll inkopplad. Skärmen är en mätare,
inte instrumentets ansikte — det kommer i fas 4.

**Pollfrekvensen, uppe till höger.** Ska stå omkring 250 Hz och "hogupplost
timer". Står det "GROV TIMER" eller under 200 Hz föll loopen tillbaka på
Windows schemaläggare, som avrundar varje väntan till ungefär 15 ms. Värdet
"varst" är den längsta lucka som uppmätts sedan nollställningen och är det som
säger sanningen — 250 Hz i snitt med en 40 ms stall i sig är ingen 250 Hz-loop.

**Åtta riktningar.** För vänsterspaken runt kanten. Sektorn ska byta en gång per
gräns och aldrig fladdra när tummen vilar på en gräns — det är hysteresen. Släpp
spaken mitt i: ackordet ska stå kvar, inte försvinna.

**Den runda grinden.** Dra högerspaken rakt ner till botten och för den sedan i
sidled. Den grå pricken är vad hårdvaran säger, den röda vad matematiken gör av
det. Den grå kan knappt röra sig i sidled längst ner — den röda ska kunna gå hela
vägen ut. Går de båda trögt är cirkel-till-kvadrat-normaliseringen trasig, och då
slutar stumningen fungera precis när plocket är som snabbast.

**Kön.** Varje knapptryck ska öka sin räknare. En knapp som lyser men inte räknar
betyder att övergången aldrig tog sig över trådgränsen.

**Ur- och isättning.** Dra ur kontrollen mitt i allt. Rubriken ska byta till
"INGEN KONTROLL" inom en sekund, och sättas den i igen ska den hittas inom en
sekund till, utan att något hänger sig.

## Thereminen

Rullgardinen i sidhuvudet byter instrument. Samma skal — sidhuvud, nivårad,
statusrad — men spakarna och triggrarna betyder nya saker.

| Kontroll | Funktion |
|---|---|
| Höger spak Y | Tonhöjd |
| Höger spak X | Böj ±1 halvton |
| LT | Volym — artikulationen |
| RT | Låsning: fri → dragning → fast |
| Vänster spak Y | Upp vibrato, ner tremolo |
| Vänster spak X | Modulationens hastighet |
| LB | Frys tonhöjden (hålls) |
| A / B / X | Skalfamiljer |
| Y | Omfång: 1–4 oktaver |
| MENY | Glidtid |
| LS-klick | Lås tonen ljudande |
| RB | Rörelse på/av (Joy-Con eller Switch Pro) |

**D-pad ▲▼ tonart, D-pad ◀▶ klangfärg, VIEW register och RS-klick tystar** —
samma i båda instrumenten, så ett byte kostar dig spakarna och triggrarna och
inget annat.

**Varför låsningen är en dragning och inte en brytare.** En tumspak ger åtta till
nio bitars repeterbart läge. Fyra oktaver på en axel blir ungefär tio cent per
mätbart steg, vilket hörs som trappsteg i långsamma glid och gör det i praktiken
omöjligt att spela rent på gehör. Kvantiseringen är alltså inte en bekvämlighet
ovanpå instrumentet — den är det som gör det spelbart. Vid halva vägen lutar
tonhöjden mot närmaste skalton utan att nå den, så ett avsiktligt böj böjer
fortfarande och en oavsiktlig darr kommer hem. Skalpanelen ritar både den råa
tonhöjden och den dragna, så du ser vad triggern gör.

Glidet finns kvar även vid full låsning. En theremin som stegar mellan toner har
slutat vara en.

### Rörelse med Joy-Con

Med ett par Joy-Con blir thereminen en theremin på riktigt: höger hand är
tonhöjdsantennen och vänster hand är volymantennen. Håll Joy-Con lösa, en i
varje hand, pekande framåt med knapparna uppåt, och tryck R för att slå på
rörelse. Hur händerna står just då blir hemläget.

- **Höger hand:** peka uppåt för högre ton. 50 grader åt vardera hållet från
  hemläget är hela omfånget, och Y väljer hur många oktaver det är. Vrider du
  handleden böjer du tonen upp till en halvton.
- **Vibrato får du genom att darra på handleden.** Tonhöjden följer handens
  vinkel direkt, utan utjämning, så din egen darrning blir vibrato. Vänster spaks
  vibrato och tremolo finns kvar ovanpå.
- **Vänster hand:** i hemläget är det tyst. Lyfter du handen 45 grader är det
  fullt, med samma kurva som triggern.
- LB fryser tonen och ZR låser mot skalan, precis som utan rörelse. En Switch Pro
  Controller eller Steam Controller har bara en uppsättning sensorer. På dem
  styr sensorerna tonhöjden, och volymen ligger kvar på vänster trigger.

Riktningen räknas fram ur gyro och accelerometer tillsammans, i
`input/MotionFusion.h`. Accelerometern vet var nedåt är men tar en skakning för
en lutning. Gyrot följer skakningen men driver iväg på några sekunder. Filtret
låter gyrot vrida skattningen och accelerometern dra den tillbaka långsamt. När
accelerometern visar mer eller mindre än en g ignoreras den helt, för då mäter
den handen och inte golvet.

**Den ursprungliga Steam Controllern** fungerar också, med sladd, trådlös
mottagare eller Bluetooth. **Steam måste vara avstängt.** Annars tar Steam Input
över kontrollen, och den dyker upp som en Xbox-kontroll utan gyro. Den har bara
en uppsättning sensorer, så lutningen styr tonhöjden och vibraton, medan volymen
ligger kvar på vänster trigger. Triggern är analog på den här kontrollen, så det
fungerar bra.

Vänster platta är d-pad och höger platta är höger spak, och panelen skriver ut
dem så. Kontrollens musläge stängs av så länge Chordtroller läser den och slås
på igen när appen släpper den.

Joy-Con och Steam Controller läses genom SDL3, som CMake hämtar vid första
konfigureringen. Xbox-pads
läses fortfarande direkt genom XInput och har företräde när båda sitter i. Vill
du bygga utan SDL: `-DCHORDTROLLER_WITH_SDL=OFF`.

**Två begränsningar med Joy-Con:**
- ZL och ZR är knappar, inte analoga triggers, så allt som läser en trigger går
  bara av eller på. Det gäller volymen i Chordtroller och filtren i Juno och
  303:an.
- Panelerna skriver ut Nintendos knappnamn bara i thereminen. De andra tre
  instrumenten visar fortfarande Xbox-namnen.

## Juno

Tredje instrumentet i rullgardinen: en ackordgenerator på en Juno-röst, med
Roland J-6 som förebild. Vänster tumme väljer ackord, höger tumme väljer stil.
Spaken *är* spelandet: släpp den så tystnar det, om inte höger spakklick håller
frasen. Triggrarna är filtret, båda två.

| Kontroll | Funktion |
|---|---|
| Vänster spak | Ackord — kvintringen från Chordtroller |
| Höger spak | Stil — riktningen väljer, ute alls är spela |
| RT | Cutoff, från stängt |
| LT | Resonans, från noll |
| LB (håll) | Utökade ackord |
| RB | Tysta allt |
| A | Chorus: av · I · II · I+II |
| B | Genre |
| X | Basnot |
| Y | Dur/moll |
| D-pad ▲▼ · ◀▶ | Tonart · patch |
| VIEW | Oktav |
| MENY | Variation av stilen |
| LS · RS | Lås ackordet · håll frasen |

**Genren sitter i ackorden, stilen i rytmen** — J-6:ans uppdelning. Ringens åtta
platser står still; genren (POP, SYNTHWAVE, HOUSE, NEO SOUL, GOSPEL, LOFI)
bestämmer vilken färg varje funktion får, i `music/JunoChords.h`. Stilen säger
bara hur ackordet spelas: PULS, ARP 1/8, ARP 1/16, RYTM, SYNKOP, STRUM, GATE,
FRAS, med fyra variationer var i `dsp/JunoPhrases.h`. Alla stilar delar samma
slagrutnät, så att byta stil mitt i takten behåller slaget.

**Båda triggrarna börjar på noll.** Triggern äger hela området, så hela svepet
ligger under fingret i stället för bara den del som råkade ligga ovanför vad
patchen redan höll — halva resan på att ta sig tillbaka till där ljudet började
är halva triggern bortkastad. Patcherna har därför inget eget viloläge på
filtret.

Stängt betyder inte tyst: patchens egen envelop läggs på ovanpå detta inne i
rösten, och det är den som skiljer patcherna åt där nere. Med båda triggrarna
hemma hamnar PLOCK på 1 430 Hz och STOT på 777 Hz, medan PAD ligger på 77 Hz och
STRAKAR på 37 Hz — de två sista är i praktiken tysta tills du trycker in RT.

Cutoffen är linjär över sin resa. Resonansen är det inte: en stege gör nästan
ingenting under en tredjedel upp, så en linjär trigger hade lagt sin första halva
på ingenting och sina sista millimetrar på allt. Avsmalningen lägger området där
fingret är — halvvägs in ligger resonansen på 0,63, i botten på 0,95, strax under
självsvängning.

Uträkningen bor i `filterSweep` i `dsp/JunoVoice.h`, och panelen anropar den i
stället för att räkna själv.

**Ingen anslagskontroll.** Hur hårt det spelas är en konstant. Det har suttit på
högerspakens avstånd och på höger trigger, och båda var värda mindre än en egen
filterkontroll — en synthesizers två triggrar hör hemma på filtret.

**Låsen sitter på spakklicken de låser.** Klicka in vänster spak och ackordet
står kvar, klicka in höger och frasen står kvar. Hållet låg på RB, där fingret
inte kände vilken spak det frös. Det kostar den delade stommen sin panikknapp på
RS — de andra två instrumenten har sin där — men RB tar över den i Junon.

**Stämföring** väljer den omvändning av genrens form som flyttar tonerna minst,
och lyfter bara de nedersta tonerna så att en nona förblir en nona. Dm7 → G7sus4
i house behåller C, D och F och flyttar bara A till G.

**Sex röster**, som originalet — en femstämmig genreform plus bas fyller dem
exakt. Släppta röster stjäls först.

**Varje ton går ut som MIDI** på det sampel den ljöd, eftersom en ackordgenerators
första jobb i en DAW är att lämna över ackorden.

### Testa motorn utan kontroll

`JunoEngine` och allt den bygger på är ren C++ utan JUCE, så den går att köra
direkt. Ett testprogram som går igenom alla genrer, stilar och variationer, byter
ackord mitt i en fras, släpper, och trycker filtret mot självsvängning, kontrollerar
tre saker: att varje note-on får sin note-off, att ingen stil är tyst, och att
inget blir NaN. Det hittade en riktig bugg vid första körningen — händelser från
ackord- och knappändringar raderades av `beginBlock()`, vilket gjorde padden tyst
och lämnade hängande toner i värden. Därför öppnas Junons notlista nu överst i
`processBlock`, före knapparna.

Samma program sveper `filterSweep` över hela triggerresan för varje patch och
kontrollerar att en släppt trigger ger exakt noll, att båda
kontrollerna stiger hela vägen — en som ger mindre ju hårdare du trycker är
trasig — och att ingen når självsvängning av sig själv.

`tools/enginetests/phrasetest.cpp` (`build.bat phrasetest`) spelar varje stil och
variation, fritt och låst till en värd, och kontrollerar att frasen slår varje
gång figuren kommer runt. Det fångade att PULS FJÄRDEDEL och STRUM SNABB bara
slog en gång: de är ett steg långa, så steget inom figuren är alltid 0, och
klockan jämförde just det steget och såg aldrig något nytt. Nu räknar klockan
steg från taktens början.

## 303

Fjärde instrumentet är en stegsekvenser med 16 steg och en röst formad som en
TB-303. Mönstret skrivs ett steg i taget under en markör, med d-pad och de fyra
knapparna, och det går att fortsätta skriva medan loopen spelar. Båda spakarna
fungerar som rattar. LB är shift. Håller du den ändrar d-pad och knappar
inställningar i stället för att skriva steg, och panelens text byter samtidigt.

| Kontroll | Utan LB | Med LB hållen |
|---|---|---|
| D-pad ▲▼ | Tonen på steget | Tonart, ett halvt tonsteg |
| D-pad ◀▶ | Flytta markören, alla 16 steg | Oktav |
| Y | Accent av/på | Skala |
| B | Slide av/på | Glide |
| A | Paus av/på | Loopen slutar på markörens steg |
| X | Kopiera närmaste ton bakåt | Delay-takt: 1/8 · 3/16 · 1/4 |
| RS | Slumpa ett mönster (med höger spak i vila) | Rensa mönstret |
| Höger spak | Delay mix upp/ner, feedback i sidled | |
| Vänster spak | ENV MOD upp/ner, DECAY i sidled | |
| RT · LT | Cutoff · resonans, från noll | |
| MENY | Play/stop | |
| LS | Vågform: såg · fyrkant | |

RB och VIEW är lediga.

**Knapparna ligger där flickarna låg.** Y överst ger accent, B till höger ger
slide, alltså glid in i steget till höger, A nederst ger paus och X till vänster
kopierar tonen bakifrån. Knappar är snabbare än flickar eftersom spaken inte
behöver tillbaka till mitten, och två snabba tryck går lätt. När du skriver
steg ligger vänster tumme på d-paden och höger tumme på knapparna.

**Spakarna är rattar.** Du lutar spaken för att vrida, och värdet ligger kvar
när du släpper. Att en spak i vila står i mitten betyder alltså bara "rör
ingenting". Om spakens läge i stället var själva värdet skulle värdet glida mot
mitten varje gång du släpper, eftersom spaken passerar alla mellanlägen på vägen
hem. Svaret är kvadratiskt: en liten lutning justerar fint, och fullt utslag tar
ungefär en sekund över hela området.

**Delay mix är en riktig dry/wet** som håller nivån medan du vrider. Delayn
matas hela tiden, så när du vrider upp mixen hörs ekon av det som redan spelats,
som när man drar upp en returkanal. Feedback stannar strax under självsvängning.
Mix på noll är av, så knappen för delay-takt behöver inget av-läge.

**Shift syns hela tiden.** När LB hålls visas ett SHIFT-märke och knapparnas
namn byts ut. Inställningarna längst ner tappar sitt "LB+" och lyser upp.

**Tonen.** Ett steg har alltid en ton, också när det är en paus. På en paus gör
första trycket på ▲ eller ▼ steget till en ton igen, med den tonhöjd det hade.
Ett orört steg börjar på grundtonen. Därefter flyttar varje tryck tonen ett
skalsteg, upp till två oktaver över grundtonen. Mönstret sparar skalsteg och
inte toner, så skala och tonart flyttar hela linjen.

**När sekvensern står** hörs steget under markören varje gång du flyttar dit
eller ändrar det, så att en linje kan skrivas på gehör. När den spelar hörs
ändringen nästa gång spelhuvudet kommer dit.

**Slumpa** ger ett nytt mönster över alla 16 steg, byggt som en basgång:
- första steget är alltid grundtonen, så loopen har en etta att komma hem till
- tonerna lutar mot grundtonen och oktaven
- pauser kommer tillräckligt ofta för att det ska andas
- slide läggs bara på steg som följs av en ton

Hur tätt det spelar, hur ofta det blir accent och slide och hur högt det går
slumpas också vid varje tryck. Nästa tryck ger alltså en ny sorts linje, inte
samma linje omblandad.

Det finns ingen ångra, så både slumpa och rensa gäller direkt. Rensa kräver två
knappar samtidigt. Slumpa sker bara när höger spak står i vila. Den spaken vrider
delayn, och ett klick som råkar gå in medan du vrider hade annars kastat ett
färdigt mönster.

**Loopens längd** sätts med LB + A på steget där loopen ska sluta. Markören går
alla 16 steg oavsett längd, så att loopen kan göras längre igen. Steg utanför
loopen finns kvar och visas dämpade.

**Mönstret sparas.** I en DAW följer det med projektet. I standalone-appen
sparas det när fönstret stängs och läses in när appen startar. Det som sparas är
stegen, längden och det som behövs för att stegen ska bli samma toner igen:
skala, oktav och tonart. Ljudet sparas inte, så glide, delay, vågform, ENV,
DECAY och nivå börjar om från sina grundvärden, och appen startar alltid på
CHORDTROLLER. En app som dödas i stället för att stängas hinner inte spara.

Värden sparar när den vill och från vilken tråd den vill. Därför läser sparandet
det som ljudtråden har publicerat, inte motorn direkt. En återställning parkeras
under ett spinnlås och hämtas av ljudtråden överst i nästa block. Det publicerade
mönstret sätts redan när pluginet skapas. Annars skulle atomernas nollor läsas
som sexton toner på grundtonen, och ett projekt som sparades innan 303:an öppnats
skulle få dem i stället för det tomma mönstret.

**Rösten** har två envelopper, som originalet. Volymen följer gaten, medan
filtret bara klingar av.
- Accent tvingar filterenvelopen kort och laddar en liten kondensator som ger
  svepet dess "wow". Kondensatorn bygger på sig när accenterna kommer tätt.
- Slide håller gaten öppen och glider till den nya tonhöjden utan att starta om
  envelopperna.
- Filtret är samma stege som Junons men med mindre återkoppling. Det är ingen
  modell av 303:ans diodstege, bara stämt åt det hållet.
- Rösten har ingen chorus och inget rum. En 303 är torr, och delayn står för
  rymden.

**MIDI ut** skickar slides som överlappande noter, vilket en värd läser som
legato.

### Testa 303-motorn utan kontroll

`AcidEngine` är ren C++ utan JUCE. Testprogrammet kontrollerar:
- att upp och ner väljer ton och stannar vid gränserna
- att markören når förbi loopens slut
- att accent, slide, paus och kopiera gör rätt sak
- att slumpade mönster följer reglerna över 3 000 tryck i alla skalor:
  grundton på första steget, ingen slide in i en paus, alla toner inom
  räckvidden och ett nytt mönster vid varje tryck
- loopslutet och uppspelning av bindningar, accenter och slides
- att det bara provlyssnas när sekvensern står
- laddning av ett sparat mönster
- 2 000 steg med slumpmässiga ändringar under spel, där varje note-on måste få
  sin note-off
- att samma session ger exakt samma mönster och toner med två olika
  blockstorlekar

## Stråkar

Femte instrumentet är en stråkad sträng gjord med fysisk modellering: fiol,
altfiol, cello eller kontrabas. Vänster tumme är vänsterhanden, som trycker ner
strängen. Höger tumme är stråken.

| Kontroll | Funktion |
|---|---|
| Vänster spak | Tonen. Åtta riktningar är åtta skalsteg: uppåt är grundtonen och sedan går skalan uppåt medurs, så snett upp till vänster är oktaven i en durskala. I mitten ligger fingret kvar. |
| Höger spak ▲▼ | Stråken. Uppåt är uppstråk, nedåt nedstråk. Ju längre ut, desto snabbare stråke och starkare ton. |
| Höger spak ▶ | Vibrato. Ju längre ut, desto djupare. |
| Höger spak ◀ | Tremolo. Stråken går fram och tillbaka av sig själv, och ju längre ut, desto fortare och starkare. |
| RT | Stråktryck: ytljud, full ton, skorr |
| LT | Glid mellan tonerna, från ett finger som landar till en glissando på en kvarts sekund |
| LB / RB (håll) | Oktav ner / oktav upp |
| LS-klick | Lås tonen som bordunsträng, så att du spelar dubbelgrepp mot den |
| RS-klick | Arco eller pizzicato |
| A / B / X | Skalfamiljer, samma som i thereminen |
| Y | Sordin |
| D-pad ▲▼ · ◀▶ | Tonart · fiol, altfiol, cello, kontrabas |
| VIEW | Register |
| MENY | Solo eller sektion |

**Ingenting här är ett sampel eller en envelop.** Strängen är en vågledare: en
slinga av vandrande vågor som delas där stråken ligger, med fingret i ena
änden och stallet i den andra. Stråken är en friktionslag som räknas ut varje
sampel mot vågorna som når den. Därför fastnar strängen i stråktagelet och
slirar loss en gång per period, och det är Helmholtz-rörelsen, alltså själva
fiolljudet. Det luftiga ytljudet vid lätt tryck och skrapet i ett slarvigt
anslag är samma ekvation med andra siffror. Ingen av dem är programmerad för
sig. Kroppen är en uppsättning resonanser efter en fiols
uppmätta: luftmoden runt 275 Hz, de två starka plattmoderna kring 500 Hz och
den breda "stallpuckeln" runt 2,5 kHz. De större instrumenten har samma form
förskjuten nedåt. Sordinen dämpar stallpuckeln, vilket är hela dess ljud.

**RT är pekfingret som lutar sig mot stråken.** Det är så en violinist ger
tryck. Trycket räknas som en andel av det mesta tagelet kan hålla vid den
fart stråken har, alltså Schellengs övre gräns. Därför ligger full ton på samma
ställe på triggern oavsett om stråken går fort eller långsamt. Triggern är
utlagd efter vad modellen faktiskt gör: första fjärdedelen är ytljud, mitten
full ton och sista fjärdedelen skorr. Strecken på RT-stapeln visar gränserna.
Släppt ger triggern fortfarande ljud, bara luftigt, så även här börjar den från
noll.

**Skorret är det enda som inte är fysik.** Sista fjärdedelen tryckte först
stråken förbi gränsen, in i det riktiga knarret. Det är kaos: det upprepar sig
aldrig och låter som knaster i en högtalare. Två försök att få kaoset att
rassla i takt gav samma sak, både tryckstötar över gränsen och en stråke som
skakar i farten, eftersom varje stöt blir ett nytt stråkanslag och de är aldrig
lika. Nu stannar strängen i ren ton, med trycket upp mot 0,78 av gränsen, och
skorret läggs på det strängen skickar till kroppen: en asymmetrisk mättning,
som något löst som surrar mot strängen, som pulserar runt 28 Hz i en takt som
delar tonhöjden, som ett rullande R. En fast kurva över en periodisk ton med en
periodisk puls blir själv periodisk, så skorret har tonhöjd, och kroppen färgar
det. Testet kontrollerar att alla fyra instrumenten behåller sin period med
RT i botten.

**Varje drag lutar sig in.** Första versionen av skorret lät rätt i
lyssningsfilen men som knaster i appen. Filen drog upp RT medan tonen redan
klingade, men den som spelar håller RT nere och drar sedan, eller byter stråk.
Då möter fullt tryck en stråke som knappt rör sig, alltså Guettlers kvävda
anslag, och strängen blev kaotisk i upp till en sekund, som skorret sedan
förstärkte. Nu börjar varje drag, från vila eller efter ett stråkbyte, på högst
en tredjedel av gränsen och lutar sig in över trettio perioder. Skorret kommer
in först med det. En fiol från vila med RT i botten gick från 0,25 periodisk
under första femtedelssekunden till 0,72, och en kontrabas från 0,08 till 0,87.
Lyssningsfiler görs nu genom själva pluginet med `ChordtrollerRender` i
`tools/panelshot`, så att de låter som appen.

Cellons och kontrabasens stråkbyten är fortfarande röriga i 0,2–0,4 s, vid
alla tryck. Det är samma tröga anslag som de lägsta tonerna har.

**Varför vibratot är automatiskt.** Med stråken och vibratot på samma tumme
kan tummen inte vicka i sidled sex gånger i sekunden utan att också rubba
stråkfarten, och då hörs vibratot som svaj i volymen. Höger spak åt höger
bestämmer därför djupet, och hastigheten ligger runt 5–6 Hz och ökar lite med
djupet, som hos riktiga spelare.

**Tremolot sitter på stråkspaken** eftersom det är en stråkteknik. Det spelar
även med spaken rakt åt vänster, utan att den är uppåt eller nedåt. I pizzicato
blir tremolot en knäppning per slag, som en mandolin.

**Pizzicato är samma sträng, knäppt.** Snärta höger spak uppåt eller nedåt från
mitten. Hur fort den når kanten avgör hur hårt det knäpps. Spaken måste
tillbaka till mitten innan den kan knäppa igen, och även när du byter läge med
RS, så att själva bytet inte knäpper.

**Släpper du stråken stannar den på strängen och dämpar den**, som på en
riktig fiol. Strängen tystnar på ungefär en kvarts sekund, och sedan står
rummets efterklang kvar. Därför behövs ingen tystaknapp, och RS-klicket kunde
bli pizzicato.

**Sektion** är fyra spelare. De är lite ostämda mot varandra, driver långsamt,
har var sitt vibrato och sin egen fördröjning i stråken, och de står utspridda
i stereo. Det är fyra fysiska strängar och ingen chorus.

**Tonerna stavas efter tonarten.** I D-dur heter de F# och C#, inte Gb och Db.
Varje stavning prövas, och den som använder varje bokstav en gång vinner.
Ackordringen stavar fortfarande allt med b, eftersom den läses som ackord runt
kvintcirkeln.

Varje ton går ut som MIDI. En stråkad ton är på så länge stråken rör sig, och
en ny ton under samma drag blir av/på på samma ställe, alltså legato. Bordunen
skickas som en egen ton. Vibrato och glid skickas inte, eftersom syntar böjer
olika långt.

### Vad testerna visade

`StringEngine` och `BowedString` är ren C++ utan JUCE, och testprogrammet går
igenom alla fyra instrumenten, fyra toner var, tre stråkfarter och elva tryck.
Det räknar hur många gånger strängen slirar per period, för det är det som
skiljer ren Helmholtz-rörelse från ytljud med flera slirningar, som har samma
tonhöjd. Fyra saker kom fram som inte hade gått att gissa:

- **Stråkens läge ska vara en andel av den ljudande längden, inte ett fast
  avstånd från stallet.** Med fast avstånd hamnade stråken på en tjugondel av
  fiolens öppna G, och Schellengs fönster mellan för lätt och för tungt blir
  smalare ju närmare stallet stråken är.
- **En stråkad sträng förlorar en liten fast andel per period.** Förlusten var
  först inställd efter hur länge ett pizzicato klingar, och det gav kontrabasens
  öppna E fem gånger så stor förlust per period som fiolens högsta ton, och ett
  fönster för smalt att spela i. Pizzicato har kvar sin egen klingtid.
- **Övertonerna dämpas vid en fast multipel av grundtonen.** Det rundar av
  hörnet i Helmholtz-vågen lika mycket på varje ton, och det är krusningen
  efter ett skarpt hörn som slår loss strängen när den ska sitta fast.
- **Trycket ska finnas där innan stråken är i fart.** Med ett tryck som växte med
  stråkens egen fart började varje ton med nästan inget grepp mot en
  accelererande stråke, och det är Guettlers recept på ett anslag som fastnar i
  flera slirningar. Stråken kommer också upp i fart över fyra perioder i stället
  för tio millisekunder.

- **Strängarna går i dubbel samplingsfrekvens.** Stråken kan bara släppa
  strängen på ett helt sampel, så en period på 129,7 sampel slirar efter 130,
  130, 129 och så vidare. Det fasta mönstret är en modulation, och den lade
  sidband 165 Hz på var sida om ett långsamt stråkat G4, bara 9 dB under tonen.
  Det är ett raspande som ingen stråke gör. I dubbel takt ligger samma mätning
  30 dB under. Det hjälpte också anslagen, som blev en tredjedel kortare.

Två saker prövades och togs bort eftersom de mätte sämre: extra tryck medan
stråken accelererar och en dämpare vid stråken.

Resultatet i mitten av RT: alla 144 kombinationer ger ren Helmholtz-rörelse.
Tonhöjden ligger inom 1 cent till och med fiolens E6. Bara toner ovanför de
vanliga registren drar iväg, som mest 10 cent. Instrumenten ligger inom 0,1 dB
av varandra, och 64 och 512 samplar per block ger exakt samma ljud.

**De lägsta tonernas anslag är långsamma.** Fiolens toner blir rena på
25–60 ms. Cellons lägsta C tar 0,17–0,26 s och kontrabasens lägsta E 0,27–0,36
s, ungefär tolv perioder, innan de slutar skrapa. Riktiga basar talar också trögt,
men det här är i överkant. Anslagstiden och extra tryck i anslaget flyttade det
inte mycket.

`tools/panelshot` har två program. `ChordtrollerShot` ritar panelen till PNG
utan fönster. `ChordtrollerCheck` spelar instrumentet genom den riktiga
processorn med en styrd låtsaskontroll i stället för `GamepadSource.cpp` och
kontrollerar vad värden får: legato, bordun, oktaver, pizzicato-snärten och att
inga toner blir hängande när instrumentet byts.

## Arpeggiator

Sjätte instrumentet bryter upp ackordet i en ton i taget. Vänster tumme håller
ackordet och höger trigger spelar: släppt står det still, och djupare går det
fortare. Höger spak är omfånget, som Omnichordens zon.

| Kontroll | Funktion |
|---|---|
| Vänster spak | Ackord, samma ring som Chordtroller och Juno |
| RT | Spela och fart: 1/8, 1/16 och 1/32 i tre steg, släppt står det still |
| Höger spak sidled | Omfång: i vila hela spannet, lutning smalnar mot den låga eller höga änden, ner till en oktav |
| Höger spak upp/ner | Tonlängd: nedåt korta prickar, uppåt legato, i vila mitten |
| LT | Filter, från stängt |
| Y · A · B · X | Sätt, två per knapp: Y uppåt/oktavhopp, A nedåt/konvergera, B pendel/pedal, X slump/ackord |
| RB | Rytm: rak, swing, punkterad (3+3+2), gate, accent, slumppauser |
| LB (håll) | Septimackord |
| LS · RS | Lås ackordet · håll arpeggiot |
| D-pad ▲▼ · ◀▶ | Tonart · ljud |
| MENY | Trummor |
| VIEW (håll) + d-pad ▲▼ · ◀▶ | Register · oktavomfång 1–4 |
| VIEW (håll) + A · X · Y | Delay · trioler · dur/moll |

**Sätten ligger på knappar, inte på en spak.** Att välja ett sätt kräver ingen
tumme som hålls ut åt ett håll. Eftersom triggern spelar kan höger tumme nå
knapparna mitt i en fras: spaken den släpper fjädrar bara tillbaka till hela
spannet och mellanlång ton. Varje knapp äger två sätt och växlar mellan dem,
som kompets figurknappar. Panelens text byter när VIEW hålls, som 303:ans
gör med LB.

**Omfånget är Omnichordens zon,** samma beteende som Chordtrollers högerspak i
sidled. Spannet sätts av oktavomfånget (VIEW + d-pad sidled, två oktaver från
start), och panelen ritar ackordtonerna som strängar med fönstret upplyst.

**Ljuden är lånade.** Plattans OMNI, HARPA, VIBRAFON och GITARR, Junons PLOCK,
STÖT, BRASS och PAD, 303:an och stråkarnas pizzicato. Varje familj drivs som i
sitt eget instrument. Junon-ljuden har sin chorus och 303:an sin accent på
rytmens betoningar. Plattan och pizzicatot saknar eget filter som en trigger kan
nå, så de delar ett tvåpoligt lågpass från 250 Hz till 18 kHz, i oktaver.
Junons patcher har kopierats med attacken kapad till 4 ms: PAD:s halvsekunds
svällning hinner aldrig fram på en sextondel. En trim per ljud, uppmätt, lägger
alla tio inom 0,2 dB av varandra.

**Farten är tre breda steg på RT,** med en marginal på var sida om varje gräns,
så ett finger som vilar nära en gräns inte flimrar mellan två farter. Strecken
på RT-stapeln visar var de ligger.

**En paus hoppar inte över en ton.** Rytmerna bestämmer vilka steg som låter,
men nästa ton i mönstret kommer ändå härnäst. Swing fördröjer vartannat steg
med en tredjedel, alltså lång–kort 2:1.

**Tempot följer värden** som i Junon och 303:an: stegen ligger på värdens
rutnät, taktlinjerat. Utan värd börjar rutnätet där du trycker. Varje ton går ut
som MIDI.

Motorn är `dsp/ArpEngine.h`. Testet `tools/enginetests/arptest.cpp` (`build.bat
arptest`) kontrollerar sätten ton för ton, rutnätet på samplet i alla sex
farterna, rytmerna, swingen, värdsynken (0,0002 sextondel), att varje not-on får
sin not-off i alla ljud, nivåerna, att 64 och 512 sampel per block ger samma
ljud, och 30 sekunder slumpade kontroller. `ChordtrollerCheck` spelar den genom
processorn: rätt toner, fart, ljudbyte, stopp, håll och instrumentbyte utan
hängande toner.

**Motortesterna ligger nu i projektet,** under `tools/enginetests`. Stråkarnas,
Junons och 303:ans testprogram låg i en tillfällig mapp som har tömts, så de
finns inte längre. Stråkarnas mätresultat står kvar i avsnittet om dem.

## Kurvorna

Allt analogt som örat hör i kvoter har logaritmisk kurva: nivåer i decibel,
frekvenser och takter i oktaver, tider som kvoter. Lika långt på reglaget ger
lika stor hörbar förändring, i stället för att det mesta händer i sista
tredjedelen.

| Reglage | Kurva |
|---|---|
| Volym på trigger, ringtid, filtrens cutoff, 303:ans decay | var redan logaritmiska |
| NIVÅ för komp, Juno, 303 och stråkar | decibel, −48 till 0 dB; var ren förstärkning |
| Plockhastighet i Chordtroller | 2–24 toner/s i lika kvoter; var kvadratisk |
| Thereminens modulationshastighet | 0,6–9 Hz i oktaver; var linjär |
| Thereminens vibrato- och tremolodjup | vibrato 4–60 cent, tremolo 1–24 dB, i kvoter |
| 303:ans delay feedback | hur länge ekot varar, 0,2–12 s i kvoter, samma i alla tempon |
| Stråkfart | 30 dB från spakens kant till fullt |
| Stråkarnas glid, vibrato, tremolotakt och pizzicato | i kvoter och decibel |
| Arpeggiatorns tonlängd (höger spak upp/ner) och filter (LT) | tonlängden i kvoter från 6 % av ett steg till legato, filtret i oktaver |

Mängder är fortfarande linjära där det är rätt: resonans, thereminens låsning,
303:ans dry/wet (som går med lika effekt) och tonhöjd, som redan är i halvtoner.

**Sparade låtar låter lite annorlunda.** Nivåerna och 303:ans feedback sparas
som reglagets läge, och ett läge betyder något annat nu. Standardlägena är
flyttade så att de låter som förut. Komp på 0,30 var −10,5 dB och är nu 0,78,
och Juno, 303 och stråkar på 0,80 var −2 dB och är nu 0,96. Men en låt som
sparats med en annan nivå får en annan volym, och där kan nivån behöva ställas
om.

## I Reason

Chordtroller fungerar som VST3-instrument i Reason 14, och tre saker kopplar ihop
dem.

**Takt och transport.** När Reason spelar följer 303:ans sekvenser Reasons
sextondelar, med steg 1 på låtens första slag, och den startar och stoppar
tillsammans med Reason. En loop som är kortare än en takt går därför i otakt mot
takterna, precis som på en 303. Junons fraser och Chordtrollers komp lägger sina
steg på Reasons takt, så en fras som startar mitt i takten kommer in där takten
är. När Reason står stilla fungerar allt som förut, och 303-panelen visar
"VÄRDEN STYR TAKTEN" medan Reason spelar.

**Automation, Remote och CV.** Pluginet har värdparametrar som Reason kan
automatisera, ta över med Remote Overrides och styra från CV Programmer:
- nivåerna för komp, Juno, 303 och stråkar
- cutoff och resonans för Juno och 303
- thereminens låsning
- stråkarnas tryck
- 303:ans env mod, decay, delay mix och delay feedback

Cutoff, resonans, låsning och stråktryck fungerar som golv, och triggern tar
resten av vägen upp därifrån. 303:ans fyra rattar delas med spakarna. Vrider
du med kontrollen medan Reason spelar in automation spelas det in, och spelar
Reason upp automation tar den över. Nivåbaren i panelen skickar till Reason på samma sätt.
Inställningar som stegas med knappar, som glide, skala och patch, är inga
parametrar än.

**Tonerna till Reasons instrument.** Reason 14 tar inte emot MIDI från
VST-plugins, vilket står rakt ut i manualen. Därför kan Chordtroller i stället
skicka sina toner till en MIDI-port i Windows, som du väljer i menyn "MIDI UT"
överst i panelen. Med en virtuell MIDI-kabel, till exempel loopMIDI, läser Reason
porten som ett keyboard:

1. Skapa en port i loopMIDI.
2. I Reason, gå till *Preferences → Control Surfaces* och lägg till ett MIDI-
   keyboard med porten som ingång.
3. Välj samma port i Chordtrollers "MIDI UT".
4. Markera spåret med instrumentet du vill spela, till exempel en Europa.
   Chordtroller läser handkontrollen oavsett vilket spår som är markerat.

Stäng av Chordtrollers eget ljud med dess kanal i Reasons mixer om du bara vill
höra Reasons instrument.

Toner skickas från strängarna i Chordtroller, från Junon, från 303:an och från
stråkarna. Thereminen och kompet skickar inga toner. Windows egna loopback-portar i MIDI
Services syns inte för vanliga MIDI-program, och därför behövs ett verktyg som
loopMIDI.

Parametrarna, valt instrument och vald MIDI-port sparas med låten.

## Vad fas 2 ska bevisa

Håll in RT och spela. Kriteriet ur planen är att du ska kunna spela en ii–V–I
med svep och stumt plock utan att något knäpper, och att en snärt ger exakt en
ton.

**En snärt, en ton.** Knäpp till högerspaken uppåt och släpp. Precis en ton.
Regeln som gör det möjligt är att varje riktningsändring avfyrar en ton direkt —
utan den kan en hastighetsmappning bara betyda "väldigt långsamt", aldrig "en
gång".

**ii–V–I.** Ringen är kvintordnad med tonikan rakt upp, så progressionen är en
kort moturs båge: höger (Dm), snett upp höger (G), rakt upp (C).

**Rullningen.** Svep långsamt uppåt i C-dur och titta på tonrutan. Den ska gå
C3, E3, G2, C4, E4, G3 — två steg upp och ett fall tillbaka under, om och om
igen. Går den rakt uppför en skala är plattans ordning fel.

**Ringlängden.** Håll in LT medan något klingar. Det som redan låter ska kortas
av också, inte bara nästa ton.

**Svepfönstret.** Luta högerspaken i sidled medan du sveper. Fönstret krymper
mjukt från tretton strängar ner mot tre och nålas fast i den ände du lutar mot,
så svepet dras ihop till en repeterande figur lågt eller högt. Ingenting hoppar i
mitten — i viloläge *är* fönstret hela plattan, och första millimetern tar bort
en sträng i andra änden. Elva bredder är taket, och det är plattans tak och inte
axelns: strängar är hela saker och det finns tretton av dem.

Fönstret ska gå att styra även när spaken ligger i botten — det är hela poängen
med kvadratnormaliseringen.

**Utklingningen.** Släpp RT mitt i något som klingar. Det ska stänga mjukt och
reverbet ska leva vidare, inte kapas. Volymen ligger före rummet just därför:
släpper du triggern slutar du mata rummet men tömmer det inte.

**Rösterna.** D-pad vänster och höger stegar mellan OMNI, HARPA, VIBRAFON och
GITARR. Vibrafonen ringer längst, gitarren kortast — rösten skalar den längd LT
ställt in, den ersätter den inte.

**Vibrationen.** En knackning per ton, som ska kännas som strängar och inte som
ett larm. Den skickas från polltråden, aldrig från audiotråden, och kan stängas
av med tickboxen nere till höger i plattpanelen.

**Septimbanken.** Håll LB. Samma åtta riktningar, färgade — Cmaj7, G7, Dm7,
Am7, Em7, Bm7b5, Bbmaj7, Fmaj7. Momentan och inte låsande, för det är vad handen
vill: håll, färga, släpp.

**Moll.** Y växlar dur och moll, parallellt och inte parallellt-relativt: C-dur
blir c-moll, inte a-moll. I c-moll ger ringen Cm, G, Ddim, Db, Ab, Eb, Bb, Fm —
harmonisk moll med en neapolitansk sexa. Femman är **dur** i moll, för det är den
som får kadensen att landa i stället för att bara vara modal, och den ligger ett
steg moturs från hemmapositionen precis som i dur. Samma handrörelse i båda
lägena.

**MIDI-ut.** Ladda VST3:an i en DAW, armera spåret och spela in. Tonerna ska
komma med rätt timing inuti blocket, inte klumpade i blockets början, och varje
ton ska få en note-off när den klingat ut. Detta är den enda delen jag inte
kunnat verifiera själv — det kräver en värd.

**Kompet.** RB startar. A ger dronen — hållet ackord och hållen grundton, ingen
puls — som är den Omnichord-sound de flesta faktiskt tänker på. B ger vamp,
grundton på ett och tre med ackordet som svarar på två och fyra. X ger bossa.
Dronen följer vänstertummen direkt; de andra två väntar till nästa slag, för att
starta om en figur mitt i den är ett snubbel.

**Kompet ska inte dö när du släpper RT.** Triggern är en svällpedal för
strängarna, och ett komp som tystnar så fort plockhanden vilar är inget komp.
Det ligger därför utanför både svällen och chorusen och möter strängarna först i
reverbet.

**Tempot** tas från värden när en DAW spelar, annars från panelen. Klicka på
siffran och skriv, eller dra i rutan runt omkring den. Står det TEMPO VARD äger
värden tempot, och då är siffran grå och går inte att skriva i — den ska inte
låtsas ta emot ett värde den ändå tappar.

**Kompnivån** är den nedersta raden i NIVAER-panelen. Klicka var som helst på
spåret, den hoppar dit. De två raderna ovanför är mätare och inte reglage — LT
och RT visar vad triggrarna gör — så kompraden har ett handtag och de andra
inte. Grundnivåerna för drone och stötar ligger kvar var för sig i `CompEngine.h`;
det här reglaget skalar dem båda.

**Inga knäppar.** Sveppa så fort du kan fram och tillbaka över samma strängar.
En sträng som plockas om tar tillbaka sin egen röst istället för att lägga en
kopia ovanpå, och oscillatorns fas löper vidare genom omtagningen — annars
klickar det.

## Hur koden hänger ihop

```
Source/
  input/
    PadState.h        vad en handkontroll ser ut som utan plattform under
    PadMath.h         alla kurvor och trosklar, rena funktioner, inget JUCE
    GamepadSource.h   gransnittet mot plattformen
    GamepadSource.cpp XInput, dynamiskt laddad. macOS blir en gren har.
    GamepadReader.*   polltraden, seqlock for axlar, FIFO for knappar
  music/
    ChordRing.h       de atta ackorden i kvinter, och plattans tonordning
  dsp/
    VoiceSpec.h       de fyra klangfargerna, som data och inte som grenar
    PluckVoice.h      en strang: tva bandbegransade oscillatorer, pluck, lagpass
    Ensemble.h        chorusen, som ar det mesta av ljudet
    StrumEngine.h     plockklockan, strangindexet och rostpoolen
  ui/
    InputMonitor.*    matskarmen
  PluginProcessor.*   ager allt, kor plockklockan per sampel
  PluginEditor.*      haller matskarmen tills fas 4
```

Fyra saker är värda att veta innan man ändrar något:

**Plattans ordning är inte pitchordning.** Varje oktav ger grundton, ters, och
sedan en färgton lagd en oktav *under* grundtonen innan den hoppar upp en oktav.
Färgtonen är kvinten i en treklang och septiman i ett septimackord — och i ett
septimackord finns kvinten inte på plattan alls. Plattan har tretton positioner
oavsett ackordtyp. Det är OM-84:ans eget mönster, rekonstruerat ur
samplingsuppsättningen på github.com/benjamindehli/Omni-84, och det är därför ett
svep rullar istället för att klättra. Hela härledningen står i kommentaren över
`StringSet` i `music/ChordRing.h` — läs den innan du "rättar" sorteringen.

**MIDI-ut parar varje ton med en nedräkning till sin egen note-off.** Strängarna
klingar av, de släpps inte, så ingenting talar om när en ton tog slut. Räknaren
lever mellan block, och slås samma ton an igen innan den löpt ut släpps den
första — en sträng kan inte ljuda två gånger.

**Kön har exakt en konsument, och det är audiotråden.** Skärmen får räknare, inte
händelser. Läser något annat ur kön tar det knapptryck från instrumentet, och
felet ser ut som att kontrollen tappar knappar.

**Skärmen räknar inte ut något tillstånd själv.** Ackordsektorn, tonarten och
strängindexet läses från vad audiotråden publicerat. Ett gränssnitt som räknar
fram sin egen kopia av speltillståndet är ett gränssnitt som kan säga emot det du
hör, och då går det inte att avgöra vem som ljuger.

Och två saker om `input/` i synnerhet:

**Axlar och knappar korsar trådgränsen olika.** Axelvärden är en nivå — bara den
nyaste betyder något — och går genom en seqlock. Knappar är händelser, där en
missad tryckning skulle svälja ett ackordbyte, och går genom en FIFO med
tidsstämpel. Från fas 2 är den stämpeln det som placerar bytet på rätt sampel.

**Kurvorna är gissningar.** Ingen har spelat fyra oktaver med en tumme förut.
Allt som formar en råsiffra till en musikalisk storhet sitter i `PadTuning` i
`PadMath.h`, samlat, och blir värdparametrar i fas 2. Sprid dem inte ut i
anropsställena.
