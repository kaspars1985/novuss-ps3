#include "i18n.hpp"

#include <unordered_map>

namespace Novuss {
namespace I18n {

namespace {

using Dict = std::unordered_map<std::string, std::string>;

// English strings — exact keys and translations from STR.en in
// reference/strings_only.js.
const Dict& englishStrings() {
    static const Dict kEn = {
        {"tagline", "The Latvian cue board game, played by the LNF rules."},
        {"opponent", "Opponent"},
        {"vsAI", "Computer"},
        {"vsHuman", "Two players"},
        {"level", "Level"},
        {"easy", "Easy"},
        {"medium", "Medium"},
        {"hard", "Hard"},
        {"match", "Match"},
        {"sets1", "One set"},
        {"sets3", "Best of 3"},
        {"sets5", "Best of 5"},
        {"names", "Names"},
        {"p1", "Player 1"},
        {"p2", "Player 2"},
        {"computer", "Computer"},
        {"language", "Language"},
        {"start", "New match"},
        {"resume", "Resume"},
        {"rules", "Rules"},
        {"rulesTitle", "How to play"},
        {"close", "Close"},
        {"menu", "Menu"},
        {"sound", "Sound"},
        {"about", "About this site"},
        {"setInfo", "Set {n} · first to {w}"},
        {"yourShot", "{name} to shoot"},
        {"thinking", "{name} is thinking…"},
        {"placeHint", "D-Pad / R-Stick: move striker · L-Stick: aim angle · "
                      "L1/R1: fine aim · hold X/R2: charge power scale · O: cancel"},
        {"power", "Power {n}%"},
        {"aimBad", "cue over the corner — not allowed"},
        {"debtBadge", "debt {n}"},
        {"coin", "Coin toss: {name} takes the first shot."},
        {"turnOf", "{name}"},
        {"setWon", "{name} wins set {n}"},
        {"matchWon", "{name} wins the match"},
        {"replayTitle", "Set replayed"},
        {"nextSet", "Next set"},
        {"newMatch", "New match"},
        {"cont", "Continue"},
        {"pen_noContact",
         "Penalty — the striker touched none of your light pucks (2.8)."},
        {"pen_oppFirst",
         "Penalty — the striker hit an opponent's puck first (7.1.2)."},
        {"pen_oppFirstRebound",
         "Penalty — after the rebound the striker hit an opponent's puck first "
         "while you still have light pucks (7.1.3)."},
        {"pen_darkDirect",
         "Penalty — a direct shot first touched a puck in your edge zone or "
         "your dark puck in the circle (4.1, 7.1.8)."},
        {"pen_onlyDark",
         "Penalty — with only dark pucks left, play off the far cushion, or off "
         "a side cushion onto your puck in the circle (7.1.6)."},
        {"pen_strikerPocket", "Penalty — the striker fell into a pocket (7.1.4)."},
        {"pen_strikerOff", "Penalty — the striker left the table (7.1.5)."},
        {"restored", "{n} puck(s) put back where they were (4.6, 4.8, 7.1.8)."},
        {"pocketed", "Pocketed {n} — shoot again (3.16)."},
        {"oppPocketed",
         "{n} opponent's puck(s) pocketed — they count for the opponent; turn "
         "passes (6.1.2)."},
        {"off",
         "A puck flew off the table — it goes to the centre circle; turn passes "
         "(3.20, 6.1.3)."},
        {"returned",
         "The striker slid back into your edge zone — turn passes (3.15, 6.1.5)."},
        {"noOwn", "None of your pucks pocketed — turn passes (6.1.1)."},
        {"penaltyPlaced", "A penalty puck goes back at the far cushion (7.3)."},
        {"debt",
         "All your pucks are on the table, so the penalty becomes a debt (7.4)."},
        {"debtPlaced", "{n} debt puck(s) placed (7.5)."},
        {"debtPlacedOpp", "Opponent's {n} debt puck(s) placed (7.5)."},
        {"firstClear",
         "{name} cleared all eight in the first series — the opponent gets one "
         "series to match it (12.4)."},
        {"lost_814",
         "A direct shot hit a puck in the dark zone and moved other pucks too "
         "(8.1.4)."},
        {"lost_811",
         "A penalised shot knocked two or more own pucks out of the dark area "
         "(8.1.1)."},
        {"lost_bothLast",
         "Both last pucks fell, but the shot earned a penalty (12.5)."},
        {"lost_lastFoul",
         "The last puck went down on a shot that loses the turn (12.4)."},
        {"win", "{name} pocketed all eight pucks (12.3)."},
        {"win_oppLast",
         "{name}'s last puck was pocketed by the opponent, so the set goes to "
         "{name}."},
        {"win_firstClear",
         "{name} cleared in the first series and the reply fell short (12.4)."},
        {"replay_bothLast",
         "Both last pucks fell in one shot — the set is replayed (12.5)."},
        {"replay_bothClear",
         "Both players cleared in their first series — replayed with sides and "
         "first shot swapped (12.4)."},
        {"replay_stuck",
         "Both players had only dark pucks and missed three times each — the "
         "set restarts (12.6)."},
        {"invalidAim",
         "The cue must lie over your own cushion, clear of its corners (3.32)."},
        {"logTitle", "Game log"},
        {"guide", "Aim line"},
        {"hints", "Dark puck hints"},
        {"setHeader", "Set {n}"},
        {"helpers", "Helpers"},
        {"none", "none"},
        {"setsWon", "sets {n}/{w}"},
        {"onTable", "Pucks still on the table: {n}"},
    };
    return kEn;
}

// Latvian strings — exact keys and translations from STR.lv in
// reference/strings_only.js.
const Dict& latvianStrings() {
    static const Dict kLv = {
        {"tagline", "Latviešu galda spēle — pēc LNF noteikumiem."},
        {"opponent", "Pretinieks"},
        {"vsAI", "Dators"},
        {"vsHuman", "Divi spēlētāji"},
        {"level", "Līmenis"},
        {"easy", "Viegls"},
        {"medium", "Vidējs"},
        {"hard", "Grūts"},
        {"match", "Spēle"},
        {"sets1", "Viens sets"},
        {"sets3", "Līdz 2 uzvarām"},
        {"sets5", "Līdz 3 uzvarām"},
        {"names", "Vārdi"},
        {"p1", "Spēlētājs 1"},
        {"p2", "Spēlētājs 2"},
        {"computer", "Dators"},
        {"language", "Valoda"},
        {"start", "Jauna spēle"},
        {"resume", "Turpināt"},
        {"rules", "Noteikumi"},
        {"rulesTitle", "Kā spēlēt"},
        {"close", "Aizvērt"},
        {"menu", "Izvēlne"},
        {"sound", "Skaņa"},
        {"about", "Par šo vietni"},
        {"setInfo", "{n}. sets · līdz {w} uzvarām"},
        {"yourShot", "Sit: {name}"},
        {"thinking", "{name} domā…"},
        {"placeHint", "Krusts / R-Stick: pārvietot sitēju · L-Stick: mērķēt "
                      "leņķi · L1/R1: precīza mērķēšana · turēt X/R2: uzlādēt "
                      "spēka skalu · O: atcelt"},
        {"power", "Spēks {n}%"},
        {"aimBad", "kija aizsedz stūri — nedrīkst"},
        {"debtBadge", "parāds {n}"},
        {"coin", "Izloze: pirmā sitiena tiesības — {name}."},
        {"turnOf", "{name}"},
        {"setWon", "{n}. setu uzvar: {name}"},
        {"matchWon", "Spēles uzvarētājs: {name}"},
        {"replayTitle", "Setu pārspēlē"},
        {"nextSet", "Nākamais sets"},
        {"newMatch", "Jauna spēle"},
        {"cont", "Turpināt"},
        {"pen_noContact",
         "Sods — ripa neskāra nevienu tavu gaišo kauliņu (2.8.)."},
        {"pen_oppFirst",
         "Sods — ripa vispirms skāra pretinieka kauliņu (7.1.2.)."},
        {"pen_oppFirstRebound",
         "Sods — ripa pēc atsitiena vispirms skāra pretinieka kauliņu, lai gan "
         "tev vēl ir gaišie kauliņi (7.1.3.)."},
        {"pen_darkDirect",
         "Sods — ar tiešu sitienu vispirms skarts kauliņš tavā apmales zonā vai "
         "tavs tumšais kauliņš vidus aplī (4.1., 7.1.8.)."},
        {"pen_onlyDark",
         "Sods — ja palikuši tikai tumšie kauliņi, ripai jāskar pretējā apmale "
         "vai sānu apmale un tad savs kauliņš vidus aplī (7.1.6.)."},
        {"pen_strikerPocket", "Sods — ripa iekrita maisiņā (7.1.4.)."},
        {"pen_strikerOff", "Sods — ripa pārkrita pāri apmalei (7.1.5.)."},
        {"restored",
         "Atjaunota kauliņu pozīcija: {n} (4.6., 4.8., 7.1.8.)."},
        {"pocketed", "Iesists: {n} — sit vēlreiz (3.16.)."},
        {"oppPocketed",
         "Iesisti pretinieka kauliņi: {n} — tie skaitās pretiniekam, sitiena "
         "tiesības pāriet (6.1.2.)."},
        {"off",
         "Kauliņš pārkrita pāri apmalei — to noliek vidus aplī, sitiena "
         "tiesības pāriet (3.20., 6.1.3.)."},
        {"returned",
         "Ripa atpakaļgājienā pārslīdēja tavas apmales zonas līniju — sitiena "
         "tiesības pāriet (3.15., 6.1.5.)."},
        {"noOwn",
         "Neviens tavs kauliņš nav iesists — sitiena tiesības pāriet (6.1.1.)."},
        {"penaltyPlaced", "Soda kauliņš izlikts pie pretējās apmales (7.3.)."},
        {"debt",
         "Visi tavi kauliņi ir uz galda, tāpēc sods tiek skaitīts kā parāds "
         "(7.4.)."},
        {"debtPlaced", "Izlikti parāda kauliņi: {n} (7.5.)."},
        {"debtPlacedOpp", "Izlikti pretinieka parāda kauliņi: {n} (7.5.)."},
        {"firstClear",
         "{name} ar pirmo sitienu sēriju iesita visus astoņus — pretiniekam ir "
         "viena sērija atbildei (12.4.)."},
        {"lost_814",
         "Ar tiešu sitienu skarts kauliņš tumšajā laukumā, un izkustējās arī "
         "citi kauliņi (8.1.4.)."},
        {"lost_811",
         "Ar sodāmu sitienu no tumšā laukuma izsisti divi vai vairāki savi "
         "kauliņi (8.1.1.)."},
        {"lost_bothLast",
         "Abi pēdējie kauliņi iekrita, bet sitiens bija sodāms (12.5.)."},
        {"lost_lastFoul",
         "Pēdējais kauliņš iesists ar sitienu, pēc kura zūd sitiena tiesības "
         "(12.4.)."},
        {"win", "{name} iesita visus astoņus kauliņus (12.3.)."},
        {"win_oppLast",
         "Pretinieks iesita pēdējo kauliņu, tāpēc sets pienākas: {name}."},
        {"win_firstClear",
         "{name} iesita visus ar pirmo sēriju, un pretinieka atbilde neizdevās "
         "(12.4.)."},
        {"replay_bothLast",
         "Abi pēdējie kauliņi iekrita vienā sitienā — setu pārspēlē (12.5.)."},
        {"replay_bothClear",
         "Abi iesita visus ar pirmo sēriju — setu pārspēlē, mainot galda puses "
         "un pirmā sitiena tiesības (12.4.)."},
        {"replay_stuck",
         "Abiem palika tikai tumšie kauliņi, un katrs trīs reizes netrāpīja — "
         "setu sāk no jauna (12.6.)."},
        {"invalidAim",
         "Kijai jābūt virs savas apmales, neaizsedzot tās stūrus (3.32.)."},
        {"logTitle", "Spēles žurnāls"},
        {"guide", "Tēmēšanas līnija"},
        {"hints", "Tumšo kauliņu padomi"},
        {"setHeader", "{n}. sets"},
        {"helpers", "Palīgi"},
        {"none", "nav"},
        {"setsWon", "seti {n}/{w}"},
        {"onTable", "Kauliņi vēl uz galda: {n}"},
    };
    return kLv;
}

const Dict& dictFor(const std::string& lang) {
    if (lang == "lv") {
        return latvianStrings();
    }
    return englishStrings();
}

// Replace all occurrences of `{key}` in `out` with `value`.
void replaceAll(std::string& out,
                const std::string& key,
                const std::string& value) {
    const std::string token = "{" + key + "}";
    std::string::size_type pos = 0;
    while ((pos = out.find(token, pos)) != std::string::npos) {
        out.replace(pos, token.size(), value);
        pos += value.size();
    }
}

} // namespace

std::string tr(const std::string& lang,
               const std::string& key,
               const std::map<std::string, std::string>& params) {
    const Dict& dict = dictFor(lang);

    std::string result;
    auto it = dict.find(key);
    if (it != dict.end()) {
        result = it->second;
    } else {
        // Fall back to English, then to the key itself.
        const Dict& en = englishStrings();
        auto enIt = en.find(key);
        if (enIt != en.end()) {
            result = enIt->second;
        } else {
            result = key;
        }
    }

    for (const auto& kv : params) {
        replaceAll(result, kv.first, kv.second);
    }
    return result;
}

const std::vector<RulesSection>& rules(const std::string& lang) {
    // Sections are verbatim from RULES.en / RULES.lv in
    // reference/strings_only.js, except the Controls section which is adapted
    // for the PS3 DualShock 3 controller.
    static const std::vector<RulesSection> kEn = {
        {"Goal",
         "Each player has eight pucks — red or black — set in a row against "
         "the cushion on the opponent's side (1.2). Pocket all eight before "
         "your opponent does (12.3). You play the larger wooden striker from "
         "the edge zone on your own side (3.9)."},
        {"Controls",
         "D-Pad / R-Stick: move the striker anywhere in your edge zone — its "
         "hole must stay clear of the lines and it may not touch a puck "
         "(3.9, 3.10). L-Stick: aim angle. L1 / R1: fine aim. Hold X / R2: "
         "charge the power scale, release to shoot. O: cancel. The aim line "
         "and the dark-puck hints are chosen in the menu before a match and "
         "stay fixed for it."},
        {"Light and dark pucks",
         "Your puck is <b>dark</b> when it lies in the band along your own "
         "cushion or inside the centre circle and the line no longer shows "
         "through its hole (2.13). All your other pucks are <b>light</b>. "
         "With hints on, <span class=\"legend dark\"></span>marks your dark "
         "pucks. Pieces in your own edge zone, whoever owns them, may not be "
         "hit first with a direct shot (3.10); with the aim line on, it turns "
         "red when it points at one."},
        {"A correct shot",
         "The striker must first touch one of your light pucks (2.8.3). After "
         "a cushion it may first touch any of your pucks (4.2). With only dark "
         "pucks left, play off the far cushion, or off a side cushion onto "
         "your puck in the circle (2.8.4, 2.8.5). A direct shot must never "
         "first touch a piece in your edge zone or your dark puck in the "
         "circle (4.1)."},
        {"Keeping the turn",
         "Pocket at least one of your pucks and you shoot again (3.16). The "
         "turn passes if you pocket none of yours, pocket an opponent's puck "
         "(it counts for them), knock a puck off the table, or let the striker "
         "slide back over your zone line while you have no dark pucks (6.1)."},
        {"Penalties",
         "A wrong first contact, the striker in a pocket and the other faults "
         "above cost the turn and a penalty puck: one of your pocketed pucks "
         "returns to the midline at the far cushion (7.1, 7.3). With all eight "
         "still on the table the penalty becomes a debt, paid as soon as you "
         "have pocketed a puck (7.4–7.6). Pieces moved by an illegal shot may "
         "be put back (4.6, 4.8). A direct hit on a dark-zone piece that moves "
         "others as well loses the set (8.1.4)."},
        {"Sets",
         "Players change sides after every set and the first shot alternates "
         "(12.2). Clearing all eight in your first series gives the opponent "
         "one series to reply; if they clear too, the set is replayed with "
         "sides and first shot swapped (12.4). Both last pucks in one shot "
         "replays the set (12.5)."},
        {"About this version",
         "Follows the Latvian Novuss Federation rules approved on 9 March 2024 "
         "and amended on 9 March 2025, for singles. Not modelled: doubles, the "
         "30-second shot clock, warnings, and pucks landing on edge or on each "
         "other. A puck flies off the table only when it hits a cushion very "
         "hard. Where the rules leave room, this version decides as follows: "
         "pocketing your last puck on a shot that loses the turn loses the set "
         "(after the note to 12.4), and pocketing your opponent's last puck "
         "gives them the set. In online matches the server checks and plays "
         "every shot, so both players always see the same table."},
        {"About & Credits",
         "Original web game created by Kaspars Foigts (laacz.lv), part of didnt.work. "
         "Sony PlayStation 3 console version developed and ported by Kaspars Ciematnieks (ciematnieks.lv)."},
    };

    static const std::vector<RulesSection> kLv = {
        {"Mērķis",
         "Katram spēlētājam ir astoņi kauliņi — sarkani vai melni —, kas "
         "salikti rindā pie apmales pretinieka pusē (1.2.). Jāiesit maisiņos "
         "visi savi astoņi kauliņi ātrāk par pretinieku (12.3.). Sit ar lielāko "
         "koka ripu no savas puses apmales zonas (3.9.)."},
        {"Vadība",
         "Krusts / R-Stick: pārvieto ripu jebkur savā apmales zonā — tās "
         "caurumā nedrīkst būt redzamas līnijas, un tā nedrīkst skart kauliņus "
         "(3.9., 3.10.). L-Stick: mērķēšanas leņķis. L1 / R1: precīza "
         "mērķēšana. Turi X / R2: uzlādē spēka skalu, atlaid, lai sistu. "
         "O: atcelt. Tēmēšanas līniju un tumšo kauliņu padomus izvēlas izvēlnē "
         "pirms spēles, un spēles laikā tie nemainās."},
        {"Gaišie un tumšie kauliņi",
         "Tavs kauliņš ir <b>tumšs</b>, ja tas atrodas joslā gar tavu apmali "
         "vai vidus aplī un tā caurumā vairs nav redzama līnija (2.13.). "
         "Pārējie tavi kauliņi ir <b>gaiši</b>. Ar ieslēgtiem padomiem "
         "<span class=\"legend dark\"></span>iezīmē tavus tumšos kauliņus. "
         "Kauliņus tavā apmales zonā (arī pretinieka) ar tiešu sitienu nedrīkst "
         "skart pirmos (3.10.); ja tēmēšanas līnija ieslēgta, tā kļūst "
         "sarkana, kad vērsta uz tādu kauliņu."},
        {"Pareizs sitiens",
         "Ripai vispirms jāskar savs gaišais kauliņš (2.8.3.). Pēc atsitiena no "
         "apmales tā drīkst vispirms skart jebkuru savu kauliņu (4.2.). Ja "
         "palikuši tikai tumšie kauliņi, sit ar atsitienu no pretējās apmales "
         "vai no sānu apmales pa savu kauliņu vidus aplī (2.8.4., 2.8.5.). Ar "
         "tiešu sitienu nekad nedrīkst vispirms skart kauliņu savā apmales zonā "
         "vai savu tumšo kauliņu vidus aplī (4.1.)."},
        {"Sitiena tiesības",
         "Ja iesit vismaz vienu savu kauliņu, sit vēlreiz (3.16.). Sitiena "
         "tiesības pāriet, ja neiesit nevienu savu, iesit pretinieka kauliņu "
         "(tas skaitās pretiniekam), kauliņš pārkrīt pāri apmalei vai ripa "
         "atpakaļgājienā pārslīd tavas zonas līniju, kad tev nav tumšo kauliņu "
         "(6.1.)."},
        {"Sodi",
         "Nepareizs pirmais pieskāriens, ripa maisiņā un citi pārkāpumi nozīmē "
         "sitiena tiesību zaudējumu un soda kauliņu: viens tavs iesistais "
         "kauliņš atgriežas uz viduslīnijas pie pretējās apmales (7.1., 7.3.). "
         "Ja visi astoņi vēl ir uz galda, sods kļūst par parādu, ko izliek, "
         "tiklīdz kāds kauliņš iesists (7.4.–7.6.). Ar sodāmu sitienu "
         "izkustinātos kauliņus var atjaunot (4.6., 4.8.). Ja ar tiešu sitienu "
         "skar kauliņu tumšajā laukumā un izkustas arī citi, sets ir zaudēts "
         "(8.1.4.)."},
        {"Seti",
         "Pēc katra seta spēlētāji mainās galda pusēm, un pirmā sitiena "
         "tiesības mainās (12.2.). Ja ar pirmo sitienu sēriju iesit visus "
         "astoņus, pretiniekam ir viena sērija atbildei; ja arī viņš iesit "
         "visus, setu pārspēlē, mainot puses un pirmā sitiena tiesības (12.4.). "
         "Ja vienā sitienā iekrīt abi pēdējie kauliņi, setu pārspēlē (12.5.)."},
        {"Par šo versiju",
         "Atbilst LNF novusa noteikumiem, kas apstiprināti 2024. gada 9. martā "
         "ar 2025. gada 9. marta papildinājumiem, vienspēlei. Nav iekļauts: "
         "dubultspēles, 30 sekunžu laika limits, brīdinājumi, kauliņi uz "
         "sāniem vai uzkrituši cits uz cita. Kauliņš pārkrīt pāri apmalei tikai "
         "pēc ļoti spēcīga trieciena. Kur noteikumi atstāj vietu "
         "interpretācijai: ja pēdējais kauliņš iesists ar sitienu, pēc kura "
         "zūd sitiena tiesības, sets ir zaudēts (pēc 12.4. piezīmes); ja iesit "
         "pretinieka pēdējo kauliņu, sets pienākas pretiniekam. Tiešsaistes "
         "spēlē katru sitienu pārbauda un izspēlē serveris, tāpēc abi "
         "spēlētāji vienmēr redz vienu un to pašu galdu."},
        {"Par spēli un autoriem",
         "Oriģinālās tīmekļa spēles izveidotājs ir Kaspars Foigts (laacz.lv), daļa no didnt.work. "
         "Sony PlayStation 3 konsoles versiju izstrādāja un portēja Kaspars Ciematnieks (ciematnieks.lv)."},
    };

    return (lang == "lv") ? kLv : kEn;
}

} // namespace I18n
} // namespace Novuss
