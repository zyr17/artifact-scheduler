#include <algorithm>
#include <tuple>
#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <unordered_map>
#include <vector>
#include <random>
#include <ctime>

#ifdef __clang__
#include <emscripten/bind.h>
using namespace emscripten;
#else
#include <omp.h>
#endif

#ifdef _WIN32
#include <format>
using std::format;
#else
#define FMT_HEADER_ONLY
#include "fmt/core.h"
using fmt::format;
#endif

namespace DATA {

    const int AFFIX_NUM = 4; // max affix number
    const int AFFIX_UPDATE_MIN = 7, AFFIX_UPDATE_MAX = 10; // update weight range
    const int AFFIX_MAX_UPGRADE_TIME = 5;

    enum class SET_NAMES { start, flower, plume, sands, goblet, circlet, end };
    const int SET_NUMBER = static_cast<int>(SET_NAMES::end) - static_cast<int>(SET_NAMES::start) - 1;
    enum class AFFIX_NAMES {
        start,
        hp, atk, def,
        hpp, atkp, defp,
        em, er, cr, cd,
        hb,
        pyroDB, hydroDB, electroDB, anemoDB, cryoDB, geoDB, physicalDB, dendroDB,
        end,
    };
    const std::unordered_map<std::string, SET_NAMES> string_to_set_names = {
        {"flower", SET_NAMES::flower},
        {"plume", SET_NAMES::plume},
        {"sands", SET_NAMES::sands},
        {"goblet", SET_NAMES::goblet},
        {"circlet", SET_NAMES::circlet},
    };
    const std::unordered_map<std::string, AFFIX_NAMES> string_to_affix_names = {
        { "hp", AFFIX_NAMES::hp },
        { "atk", AFFIX_NAMES::atk },
        { "def", AFFIX_NAMES::def },
        { "hpp", AFFIX_NAMES::hpp },
        { "atkp", AFFIX_NAMES::atkp },
        { "defp", AFFIX_NAMES::defp },
        { "em", AFFIX_NAMES::em },
        { "er", AFFIX_NAMES::er },
        { "cr", AFFIX_NAMES::cr },
        { "cd", AFFIX_NAMES::cd },
        { "hb", AFFIX_NAMES::hb },
        { "pyroDB", AFFIX_NAMES::pyroDB },
        { "hydroDB", AFFIX_NAMES::hydroDB },
        { "electroDB", AFFIX_NAMES::electroDB },
        { "anemoDB", AFFIX_NAMES::anemoDB },
        { "cryoDB", AFFIX_NAMES::cryoDB },
        { "geoDB", AFFIX_NAMES::geoDB },
        { "physicalDB", AFFIX_NAMES::physicalDB },
        { "dendroDB", AFFIX_NAMES::dendroDB },
    };
    // source https://genshin-impact.fandom.com/wiki/Artifacts/Distribution
    const std::vector<std::pair<int, int>> INITIAL_AFFIX_NUM_WEIGHT = { {3, 4}, {4, 1} };
    // sub will not same as main, and other subs has its choose weight. when new sub is generated, choose valid one based on weight of all valid subs.
    const std::vector<std::pair<AFFIX_NAMES, int>> SUB_PROB_WEIGHT = {
        {AFFIX_NAMES::hp, 6},
        {AFFIX_NAMES::atk, 6},
        {AFFIX_NAMES::def, 6},
        {AFFIX_NAMES::hpp, 4},
        {AFFIX_NAMES::atkp, 4},
        {AFFIX_NAMES::defp, 4},
        {AFFIX_NAMES::em, 4},
        {AFFIX_NAMES::er, 4},
        {AFFIX_NAMES::cr, 3},
        {AFFIX_NAMES::cd, 3},
    };
    const std::unordered_map<SET_NAMES, std::unordered_map<AFFIX_NAMES, int>> MAIN_WEIGHT = {
        {SET_NAMES::flower, {
                {AFFIX_NAMES::hp, 1},
            }
        },
        {SET_NAMES::plume, {
                {AFFIX_NAMES::atk, 1},
            }
        },
        {SET_NAMES::sands, {
                {AFFIX_NAMES::hpp, 2668},
                {AFFIX_NAMES::atkp, 2666},
                {AFFIX_NAMES::defp, 2666},
                {AFFIX_NAMES::em, 1000},
                {AFFIX_NAMES::er, 1000},
            }
        },
        {SET_NAMES::goblet, {
                {AFFIX_NAMES::hpp, 19175},
                {AFFIX_NAMES::atkp, 19175},
                {AFFIX_NAMES::defp, 19150},
                {AFFIX_NAMES::em, 2500},
                {AFFIX_NAMES::pyroDB, 5000},
                {AFFIX_NAMES::hydroDB, 5000},
                {AFFIX_NAMES::electroDB, 5000},
                {AFFIX_NAMES::anemoDB, 5000},
                {AFFIX_NAMES::cryoDB, 5000},
                {AFFIX_NAMES::geoDB, 5000},
                {AFFIX_NAMES::physicalDB, 5000},
                {AFFIX_NAMES::dendroDB, 5000},
            }
        },
        {SET_NAMES::circlet, {
                {AFFIX_NAMES::hpp, 22},
                {AFFIX_NAMES::atkp, 22},
                {AFFIX_NAMES::defp, 22},
                {AFFIX_NAMES::em, 4},
                {AFFIX_NAMES::cr, 10},
                {AFFIX_NAMES::cd, 10},
                {AFFIX_NAMES::hb, 10},
            }
        },
    };

    template<class T>
    std::string type_to_string(std::unordered_map<std::string, T> map, T type) {
        for (auto& [i, j] : map)
            if (j == type)
                return i;
        throw std::runtime_error("type_to_string: not found");
    }

    struct Artifact {
        SET_NAMES set;
        AFFIX_NAMES main;
        std::vector<std::pair<AFFIX_NAMES, int>> sub;
        int level;

        Artifact() = default;
        Artifact(const Artifact& a) = default;
        Artifact(
            SET_NAMES set, AFFIX_NAMES main,
            const std::vector<std::pair<AFFIX_NAMES, int>>& sub, int level) :
            set(set), main(main), sub(sub), level(level) {}

        // construct with string output by to_string
        Artifact(const std::string& art_str) {
            std::string s = art_str;
            for (auto& i : s)
                if (i == '|')
                    i = ' ';
            std::istringstream sin(s);
            std::string set_name, set_data, lv_name, main_name, main_data, sub_name;
            int lv_data;
            sin >> set_name >> set_data >> lv_name >> lv_data >> main_name >> main_data >> sub_name;
            if (set_name != "SET" || lv_name != "LV" || main_name != "MAIN" || sub_name != "SUB")
                throw std::runtime_error("wrong format: " + art_str);
            set = string_to_set_names.find(set_data)->second;
            level = lv_data;
            main = string_to_affix_names.find(main_data)->second;
            while (1) {
                std::string sub_data = "";
                sin >> sub_data;
                if (!sub_data.size()) break;
                auto comma_pos = sub_data.find(",");
                auto sub_affix = string_to_affix_names.find(sub_data.substr(comma_pos + 1))->second;
                auto sub_value = std::stoi(sub_data.substr(0, comma_pos));
                sub.push_back({ sub_affix, sub_value });
            }
        }

        std::string to_string() {
            std::string substr = "";
            for (auto& [name, weight] : sub) {
                if (substr.size()) substr += "|";
                substr += format("{:-2d},{:4s}", weight, type_to_string(string_to_affix_names, name));
            }
            if (sub.size() < AFFIX_NUM) substr += "|";
            return format("SET {:7s}|LV {}|MAIN {:10s}|SUB {}",
                type_to_string(string_to_set_names, set),
                level,
                type_to_string(string_to_affix_names, main),
                substr
            );
        }
    };

    std::random_device rd;
    std::mt19937 mt(rd());
    std::uniform_real_distribution<double> rand_real_dist(0, 1);
    std::normal_distribution<double> rand_normal_distribution_(0, 1);

    // uniformly return 0~max-1
    inline auto randint(int max) {
        std::uniform_int_distribution<int> rand_int_dist(0, max - 1);
        return rand_int_dist(mt);
    }

    // return random [0, 1)
    inline auto rand() {
        return rand_real_dist(mt);
    }

    inline auto rand_normal_distribution(double u = 0, double sigma = 1) {
        auto r = rand_normal_distribution_(mt);
        return r * sigma + u;
    }

    // vec contains first T second weight. will choose T by weight
    template<class T, class V>
    inline V weighted_sum(const std::vector<std::pair<T, V>>& vec) {
        V sum = 0;
        for (auto& [i, j] : vec)
            sum += j;
        return sum;
    }

    // vec contains first T second weight. will choose T by weight
    template<class T>
    inline T weighted_rand(const std::vector<std::pair<T, int>>& vec) {
        int sum = weighted_sum(vec);
        int ret = randint(sum);
        for (auto& [i, j] : vec) {
            if (ret < j) return i;
            ret -= j;
        }
        throw std::runtime_error("error in weighted_rand");
    }

    auto get_random_set() {
        auto res = randint(SET_NUMBER);
        return static_cast<SET_NAMES>(res + 1);
    }

    auto get_main_distribution(const SET_NAMES set) {
        const auto& main_weight = MAIN_WEIGHT.find(set)->second;
        std::vector<std::pair<AFFIX_NAMES, int>> main_vec;
        for (auto& elem : main_weight)
            main_vec.push_back(elem);
        return main_vec;
    }

    auto get_sub_distribution(const AFFIX_NAMES main, const std::vector<AFFIX_NAMES>& sub) {
        std::vector<std::pair<AFFIX_NAMES, int>> sub_vec;
        for (auto& [i, j] : SUB_PROB_WEIGHT)
            if (i != main && std::find(sub.begin(), sub.end(), i) == sub.end())
                sub_vec.push_back({ i, j });
        return sub_vec;
    }

    template <class T>
    auto get_weight_from_distribution(const T key, const std::vector<std::pair<T, int>>& vec) {
        for (auto& [k, v] : vec)
            if (k == key)
                return v;
        throw std::runtime_error("error in get_weight_from_distribution");
    }

    // random one artifact. can specify some keys, and if find key conflict (e.g. set is flower but main is not hp),
    // will throw runtime error.
    auto random_one_artifact(SET_NAMES set = SET_NAMES::end, AFFIX_NAMES main = AFFIX_NAMES::end, int initial = 0,
        std::vector<std::pair<AFFIX_NAMES, int>> sub = std::vector<std::pair<AFFIX_NAMES, int>>()) {
        if (set == SET_NAMES::end)
            set = get_random_set();
        auto main_dist = get_main_distribution(set);
        if (main == AFFIX_NAMES::end)
            main = weighted_rand(main_dist);
        else
            get_weight_from_distribution(main, main_dist); // if chosen main not in dist, throw error
        std::vector<AFFIX_NAMES> sub_affix;
        if (!initial)
            initial = weighted_rand(INITIAL_AFFIX_NUM_WEIGHT);
        else
            get_weight_from_distribution(initial, INITIAL_AFFIX_NUM_WEIGHT);
        if (sub.size() > initial)
            throw std::runtime_error("sub number too much");
        for (int i = 0; i < initial; i++) {
            auto dist = get_sub_distribution(main, sub_affix);
            if (i < sub.size()) {
                sub_affix.push_back(sub[i].first);
                get_weight_from_distribution(sub[i].first, dist);
                if (AFFIX_UPDATE_MAX < sub[i].second || AFFIX_UPDATE_MIN > sub[i].second)
                    throw std::runtime_error("affix weight wrong");
            }
            else
                sub_affix.push_back(weighted_rand(dist));
        }
        for (int i = sub.size(); i < initial; i++)
            sub.push_back({ sub_affix[i], randint(AFFIX_UPDATE_MAX - AFFIX_UPDATE_MIN + 1) + AFFIX_UPDATE_MIN });
        return Artifact{ set, main, sub, 0 };
    }

    auto artifact_appear_rate(const Artifact& a, bool debug = false) {
        if (!(a.level == 0 && (a.sub.size() == 3 || a.sub.size() == 4)))
            throw std::runtime_error("level not zero or sub number wrong");
        double rate = 1, orate;

        // set rate
        orate = rate;
        rate /= SET_NUMBER;
        if (debug) std::cout << format("{:11.4f}|", rate / orate);

        // affix number rate
        orate = rate;
        rate *= get_weight_from_distribution(static_cast<int>(a.sub.size()), INITIAL_AFFIX_NUM_WEIGHT);
        rate /= weighted_sum(INITIAL_AFFIX_NUM_WEIGHT);
        if (debug) std::cout << format("{:.2f}|", rate / orate);

        // main rate
        orate = rate;
        const auto main_dist = get_main_distribution(a.set);
        rate *= get_weight_from_distribution(a.main, main_dist);
        rate /= weighted_sum(main_dist);
        if (debug) std::cout << format("{:15.4f}|SUB ", rate / orate);

        // sub rate
        std::vector<AFFIX_NAMES> calculated_subs;
        for (auto& [name, weight] : a.sub) {
            auto dist = get_sub_distribution(a.main, calculated_subs);

            // type rate
            orate = rate;
            rate *= get_weight_from_distribution(name, dist);
            rate /= weighted_sum(dist);
            if (debug) std::cout << format("{:7.4f}|", rate / orate);

            // weight rate
            // rate /= AFFIX_UPDATE_MAX - AFFIX_UPDATE_MIN + 1;

            calculated_subs.push_back(name);
        }

        return rate;
    }

    // count sub appear rate with randomization
    auto check_sub_appear_rate(int initial_sub_number = 4, int sim_time = 1000000, SET_NAMES set = SET_NAMES::end, AFFIX_NAMES main_affix = AFFIX_NAMES::end) {
        std::map<AFFIX_NAMES, int> m;
        int times = sim_time, initial = initial_sub_number;
        for (int i = 0; i < times; i++) {
            auto art = random_one_artifact(set, main_affix, initial);
            for (auto& [name, weight] : art.sub) {
                if (m.find(name) == m.end())
                    m[name] = 0;
                m[name] ++;
            }
        }
        return m;
    }

    auto generate_all_possible_sub_orders(
        const int update_number, const AFFIX_NAMES main,
        std::vector<AFFIX_NAMES> current_sub = std::vector<AFFIX_NAMES>(), const double current_prob = 1) {
        std::vector<std::pair<std::vector<AFFIX_NAMES>, double>> res;
        if (update_number == 0) {
            res.push_back({ current_sub, current_prob });
            return res;
        }
        auto subs = get_sub_distribution(main, current_sub);
        auto sub_weight_sum = weighted_sum(subs);
        for (auto& [sub, sub_weight] : subs) {
            current_sub.push_back(sub);
            for (auto& i : generate_all_possible_sub_orders(update_number - 1, main, current_sub, current_prob* sub_weight / sub_weight_sum)) {
                res.push_back(std::move(i));
            }
            current_sub.pop_back();
        }
        return res;
    }

    std::vector<std::pair<Artifact, double>> all_artifacts_accumulated;
    std::map<SET_NAMES, std::vector<std::pair<Artifact, double>>> all_artifacts_accumulated_divided_by_set;
    bool ALL_ARTIFACTS_ACCUMULATED_DONE = false;

    // Generate all possible artifacts and calculate their posibilities.
    // As sub weight is always uniform, sub weight of generated artifacts
    // will always be AFFIX_UPDATE_MIN. should generate weight manually.
    // Sub affix will be sorted, but their order will affect its appear
    // rate, so generate artifacts with no-order, and sort their subs,
    // and combine same sub artifacts and their posibilities.
    auto generate_all_artifacts_with_probs() {

        if (ALL_ARTIFACTS_ACCUMULATED_DONE) return;

        decltype(all_artifacts_accumulated) res;
        auto initial_weight_sum = weighted_sum(INITIAL_AFFIX_NUM_WEIGHT);

        // generate all artifacts and its possibilities
        for (int setn = static_cast<int>(SET_NAMES::start) + 1; setn < static_cast<int>(SET_NAMES::end); setn++) {
            auto set = static_cast<SET_NAMES>(setn);
            auto set_count = SET_NUMBER;
            auto main_dist = get_main_distribution(set);
            auto main_weight_sum = weighted_sum(main_dist);
            for (auto& [main, main_weight] : main_dist) {
                std::vector<AFFIX_NAMES> sub_affix;
                for (auto& [initial, initial_weight] : INITIAL_AFFIX_NUM_WEIGHT) {
                    auto possible_sub_orders = generate_all_possible_sub_orders(initial, main);
                    decltype(possible_sub_orders) filtered_possible_sub_orders;
                    // sort inner subs and sort all subs pairs
                    for (auto& i : possible_sub_orders)
                        std::sort(i.first.begin(), i.first.end());
                    std::sort(possible_sub_orders.begin(), possible_sub_orders.end());

                    // for (auto& [i, w] : possible_sub_orders) {
                    //     for (auto& j : i)
                    //         std::cout << static_cast<int>(j) << ' ';
                    //     std::cout << w << '\n';
                    // }

                    // if sub same as last, add weight; otherwise push
                    for (auto& [i, w] : possible_sub_orders) {
                        if (filtered_possible_sub_orders.size() && std::equal(i.begin(), i.end(), filtered_possible_sub_orders.crbegin()->first.begin())) {
                            filtered_possible_sub_orders.rbegin()->second += w;
                        }
                        else {
                            filtered_possible_sub_orders.push_back({ i, w });
                        }
                    }

                    for (auto& [sub_affix, sub_weight] : filtered_possible_sub_orders) {
                        decltype(Artifact::sub) sub;
                        for (auto& sub_name : sub_affix)
                            sub.push_back({ sub_name, AFFIX_UPDATE_MIN });
                        auto art = Artifact{ set, main, sub, 0 };
                        auto art_rate = 1.0 / set_count * main_weight / main_weight_sum * initial_weight / initial_weight_sum * sub_weight;
                        res.push_back({ art, art_rate });
                    }
                }
            }
        }
        for (int i = static_cast<int>(SET_NAMES::start) + 1; i < static_cast<int>(SET_NAMES::end); i++)
            all_artifacts_accumulated_divided_by_set[static_cast<SET_NAMES>(i)] = std::vector<std::pair<Artifact, double>>();
        for (auto i : res) {
            auto set = i.first.set;
            i.second *= SET_NUMBER; // probability in set equals to multiply set number
            all_artifacts_accumulated_divided_by_set[set].push_back(std::move(i));
        }
        for (auto& [set, vec] : all_artifacts_accumulated_divided_by_set) {
            for (int i = 1; i < vec.size(); i++)
                vec[i].second += vec[i - 1].second;
        }
        for (int i = 1; i < res.size(); i++)
            res[i].second += res[i - 1].second;
        all_artifacts_accumulated = std::move(res);
        ALL_ARTIFACTS_ACCUMULATED_DONE = true;
    }

    // global variable is accumulated data, here return not accumulated data. 
    // if set not specified(end), return all. otherwise only this set.
    auto get_all_artifacts_with_probs(SET_NAMES set = SET_NAMES::end) {
        generate_all_artifacts_with_probs();
        std::vector<std::pair<Artifact, double>> res;
        if (set == SET_NAMES::end) {
            res = all_artifacts_accumulated;
        }
        else {
            res = all_artifacts_accumulated_divided_by_set[set];
        }
        for (int i = res.size() - 1; i >= 1; i--)
            res[i].second -= res[i - 1].second;
        return res;
    }

    // get random drop with all_artifacts_accumulated. random is double between 0 and 1.
    auto get_drop(double randnum) {
        generate_all_artifacts_with_probs();
        // avoid border and accuracy problem
        if (randnum <= all_artifacts_accumulated.begin()->second)
            return all_artifacts_accumulated.begin()->first;
        if (randnum > all_artifacts_accumulated.rbegin()->second)
            return all_artifacts_accumulated.rbegin()->first;

        int left = 0, right = all_artifacts_accumulated.size() - 1; // (left, right]
        while (left + 1 < right) {
            int p = (left + right) / 2;
            if (all_artifacts_accumulated[p].second < randnum)
                left = p;
            else
                right = p;
        }
        auto art = all_artifacts_accumulated[right].first;
        // scale randnum to 0-1, and decide affix based on its number
        randnum = (randnum - all_artifacts_accumulated[left].second) / (all_artifacts_accumulated[right].second - all_artifacts_accumulated[left].second);
        auto update_way = AFFIX_UPDATE_MAX - AFFIX_UPDATE_MIN + 1;
        for (auto& [t, w] : art.sub) {
            randnum = randnum * update_way;
            w = int(randnum);
            if (w >= update_way)
                w = update_way - 1;
            randnum -= w;
            w += AFFIX_UPDATE_MIN;
        }
        return art;
    }

    inline auto get_random_drop() {
        return get_drop(rand());
    }

}

namespace DP {

    bool DEBUG = false; // if true, output debug message

    const int N = DATA::AFFIX_MAX_UPGRADE_TIME; // max dfs depth
    const int BASE = 64; // base of affix weight

    // multiplier for weight, so can use int to approximate float
    const int SCORE_MULTIPLIER = 1;

    const double EPS = 1e-8;

    typedef double dftype; // type used by dogfood
    typedef double stype; // type used by score

    // dogfood consts
    const int DOGFOOD_COST[] = { 16300, 28425, 42425, 66150, 117175 };
    const int SUCCESS_DOGFOOD_COST = DOGFOOD_COST[0] + DOGFOOD_COST[1]
        + DOGFOOD_COST[2] + DOGFOOD_COST[3] + DOGFOOD_COST[4];
    const int FEED_DOGFOOD = 3780;
    const int DOGFOOD_LOSS[] = {
        FEED_DOGFOOD,
        FEED_DOGFOOD - (DOGFOOD_COST[0]) / 5,
        FEED_DOGFOOD - (DOGFOOD_COST[0] + DOGFOOD_COST[1]) / 5,
        FEED_DOGFOOD - (DOGFOOD_COST[0] + DOGFOOD_COST[1] + DOGFOOD_COST[2]) / 5,
        FEED_DOGFOOD - (DOGFOOD_COST[0] + DOGFOOD_COST[1] + DOGFOOD_COST[2] + DOGFOOD_COST[3]) / 5,
        FEED_DOGFOOD - (DOGFOOD_COST[0] + DOGFOOD_COST[1] + DOGFOOD_COST[2] + DOGFOOD_COST[3] + DOGFOOD_COST[4]) / 5,
    };

    bool IS_INIT = false; // if initialized
    std::map<int, int> m; // used in dfs
    // first is cell status code, second is route count
    std::vector<std::pair<unsigned int, int>> cell[N + 1];

    inline std::string status2str(int status) {
        std::string res;
        for (int i = DATA::AFFIX_NUM; i--; ) {
            if (res.size()) res += "-";
            res += format("{}", status % BASE);
            status /= BASE;
        }
        return res;
    }

    // dfs to find possible states
    void dfs(int remain, std::vector<int>& current) {
        if (remain) {
            for (int i = 0; i < DATA::AFFIX_NUM; i++)
                for (int j = DATA::AFFIX_UPDATE_MIN; j <= DATA::AFFIX_UPDATE_MAX; j++) {
                    current[i] += j;
                    dfs(remain - 1, current);
                    current[i] -= j;
                }
        }
        else {
            int id = 0;
            for (auto i : current)
                id = id * BASE + i;
            if (m.find(id) == m.end())
                m[id] = 0;
            m[id] ++;
        }
    }

    // init states
    void init() {
        if (IS_INIT) return;
        for (int n = 0; n <= N; n++) {
            std::vector<int> start;
            start.resize(DATA::AFFIX_NUM);
            m.clear();
            dfs(n, start);
            for (auto& i : m)
                cell[n].push_back(i);
        }
        IS_INIT = true;
    }

    // no need to explicitly call it. if find 3 sub artifact, calc will call this
    // function automatically. 
    std::tuple<bool, dftype, dftype, double, double> calc_3(DATA::Artifact art,
        const std::map<DATA::AFFIX_NAMES, double>& sub_scores, double score_bar, dftype gain);

    /*
    input: current weight w1 w2 w3 w4, affix score s1 s2 s3 s4, upgrade time N,
           score bar S, gain G.
           WARNING: it is recommended to call it with overload
           (artifact, score_map, score_bar, gain), otherwise it can only deal with
           4-sub artifacts.

    output: whether upgrade, expected gain, expected dogfood cost,
            success rate in current policy, expected score gain when success.
    */
    auto calc(const std::vector<int>& weight, const std::vector<double>& score,
        int upgrade_time, double score_bar, dftype gain) {

        init();

        if (weight.size() != DATA::AFFIX_NUM || score.size() != DATA::AFFIX_NUM)
            throw std::runtime_error("w or s size not equal to DATA::AFFIX_NUM");

        // multiply scores
        stype SCORE_BAR = score_bar * SCORE_MULTIPLIER;
        std::vector<stype> SCORE;
        for (auto& i : score)
            SCORE.push_back(i * SCORE_MULTIPLIER);
        for (int i = 0; i < DATA::AFFIX_NUM; i++)
            SCORE_BAR -= weight[i] * SCORE[i];

        // status, count, score in current status, used to sort
        std::vector<std::vector<std::tuple<int, int, stype>>> dp_cell;
        // key status; value count, score in current status, 
        // expected gain, expected dogfood cost, success rate, 
        // expected score gain when success
        std::vector<std::unordered_map<
            int, std::tuple<int, stype, dftype, dftype, double, stype>>> dp_map;

        dp_cell.resize(upgrade_time + 1);
        dp_map.resize(upgrade_time + 1);
        auto max_increase = *std::max_element(SCORE.begin(), SCORE.end()) * DATA::AFFIX_UPDATE_MAX;
        auto current_upgrade = N - upgrade_time;
        for (int i = upgrade_time; i >= 0; i--) {
            auto current_score_bar = SCORE_BAR - max_increase * (upgrade_time - i) - EPS;
            if (DEBUG) std::cout << format("time {}, current score bar {}\n", i, current_score_bar);
            int for_count = 0;
            for (auto& [status, count] : cell[i]) {
                stype status_score = 0;
                for (int i = 0, j = status; i < DATA::AFFIX_NUM; i++) {
                    status_score += (j % BASE) * SCORE[i];
                    j /= BASE;
                }
                dftype e_gain = 0, e_df_cost = 0;
                double success_rate = 0;
                stype e_score_gain = 0;
                for_count++;
                if (i == upgrade_time) {
                    if (status_score < current_score_bar)
                        continue;
                    // full upgraded
                    success_rate = 1;
                    e_gain = gain;
                    e_df_cost = SUCCESS_DOGFOOD_COST;
                    e_score_gain = status_score - SCORE_BAR;

                    if (DEBUG)
                        std::cout << format("DP {}: {} {} C:{} SS:{} EG:{} EDF:{} SR:{}, ESG:{}\n",
                            i, status2str(status), e_gain > DOGFOOD_LOSS[current_upgrade + i] ? "SUCC" : "FAIL", count, status_score, e_gain, e_df_cost, success_rate, e_score_gain);

                    dp_map[i][status] = {
                        count,
                        status_score,
                        e_gain,
                        e_df_cost,
                        success_rate,
                        e_score_gain
                    };
                }
                else {
                    // partial upgraded, need DP
                    auto current_base = 1;
                    auto route_number = DATA::AFFIX_NUM * (1 + DATA::AFFIX_UPDATE_MAX
                        - DATA::AFFIX_UPDATE_MIN);
                    for (int a_idx = 0; a_idx < DATA::AFFIX_NUM; a_idx++) {
                        for (int upd_w = DATA::AFFIX_UPDATE_MIN; upd_w <= DATA::AFFIX_UPDATE_MAX; upd_w++) {
                            auto new_status = status + upd_w * current_base;
                            auto target = dp_map[i + 1].find(new_status);
                            if (target == dp_map[i + 1].end()) {
                                // not in map, default not needed
                                e_gain += DOGFOOD_LOSS[current_upgrade + i + 1];
                                e_df_cost -= DOGFOOD_LOSS[current_upgrade + i + 1];
                            }
                            else {
                                auto& [t_count, t_status_score, t_e_gain,
                                    t_e_df_cost, t_success_rate, t_e_score_gain]
                                    = target->second;
                                e_gain += t_e_gain;
                                e_df_cost += t_e_df_cost;
                                success_rate += t_success_rate;
                                e_score_gain += t_success_rate * t_e_score_gain;
                            }
                            // if (DEBUG) std::cout << format("{} {} {} {} {} {}\n", a_idx, upd_w, e_gain, e_df_cost, success_rate, e_score_gain);
                        }
                        current_base *= BASE;
                    }
                    e_gain /= route_number;
                    e_df_cost /= route_number;
                    success_rate /= route_number;
                    if (success_rate > 0) e_score_gain /= route_number * success_rate;
                    if (DEBUG) std::cout << format("DP {}: {} {} C:{} SS:{} EG:{} EDF:{} SR:{}, ESG:{}\n",
                        i, status2str(status), e_gain > DOGFOOD_LOSS[current_upgrade + i] ? "SUCC" : "FAIL", count, status_score, e_gain, e_df_cost, success_rate, e_score_gain);
                    if (e_gain > DOGFOOD_LOSS[current_upgrade + i])
                        dp_map[i][status] = {
                            count,
                            status_score,
                            e_gain,
                            e_df_cost,
                            success_rate,
                            e_score_gain
                    };
                }
            }
        }
        if (dp_map[0].find(0) == dp_map[0].end()) {
            dftype gain = DOGFOOD_LOSS[current_upgrade];
            dftype df_cost = -gain;
            double s_rate = 0;
            stype score_gain = 0;
            return std::make_tuple(
                false,
                gain,
                df_cost,
                s_rate,
                score_gain * 1. / SCORE_MULTIPLIER
            );
        }
        auto& [count, status_score, e_gain, e_df_cost, success_rate,
            e_score_gain] = dp_map[0][0];
        return std::make_tuple(
            true,
            e_gain,
            e_df_cost,
            success_rate,
            e_score_gain * 1. / SCORE_MULTIPLIER
        );
    }

    /*

    deprecated version of calc, runs slower than current version

    input: current weight w1 w2 w3 w4, affix score s1 s2 s3 s4, upgrade time N,
           score bar S, gain G.
           WARNING: it is recommended to call it with overload
           (artifact, score_map, score_bar, gain), otherwise it can only deal with
           4-sub artifacts.

    output: whether upgrade, expected gain, expected dogfood cost,
            success rate in current policy, expected score gain when success.
    */
    auto calc2(const std::vector<int>& weight, const std::vector<double>& score,
        int upgrade_time, double score_bar, dftype gain) {

        init();

        if (weight.size() != DATA::AFFIX_NUM || score.size() != DATA::AFFIX_NUM)
            throw std::runtime_error("w or s size not equal to DATA::AFFIX_NUM");

        // multiply scores
        stype SCORE_BAR = score_bar * SCORE_MULTIPLIER;
        std::vector<stype> SCORE;
        for (auto& i : score)
            SCORE.push_back(i * SCORE_MULTIPLIER);
        for (int i = 0; i < DATA::AFFIX_NUM; i++)
            SCORE_BAR -= weight[i] * SCORE[i];

        // status, count, score in current status, used to sort
        std::vector<std::vector<std::tuple<int, int, stype>>> dp_cell;
        // key status; value count, score in current status, 
        // expected gain, expected dogfood cost, success rate, 
        // expected score gain when success
        std::vector<std::unordered_map<
            int, std::tuple<int, stype, dftype, dftype, double, stype>>> dp_map;

        dp_cell.resize(upgrade_time + 1);
        dp_map.resize(upgrade_time + 1);
        auto max_increase = *std::max_element(SCORE.begin(), SCORE.end()) * DATA::AFFIX_UPDATE_MAX;
        auto current_upgrade = N - upgrade_time;
        for (int i = upgrade_time; i >= 0; i--) {
            for (auto& [status, count] : cell[i])
                dp_cell[i].push_back({
                    status,
                    count,
                    [SCORE](auto status) {
                        stype res = 0;
                        for (int i = 0; i < DATA::AFFIX_NUM; i++) {
                            res += (status % BASE) * SCORE[i];
                            status /= BASE;
                        }
                        return res;
                    }(status),
                    });
            std::sort(
                dp_cell[i].begin(), dp_cell[i].end(),
                [](auto x, auto y) {
                    return std::get<2>(x) > std::get<2>(y);
                }
            );
            auto current_score_bar = SCORE_BAR - max_increase * (upgrade_time - i) - EPS;
            if (DEBUG) std::cout << format("time {}, current score bar {:.2f}\n", i, current_score_bar);
            int for_count = 0;
            for (auto& [status, count, status_score] : dp_cell[i]) {
                dftype e_gain = 0, e_df_cost = 0;
                double success_rate = 0;
                stype e_score_gain = 0;
                for_count++;
                if (status_score < current_score_bar) {
                    if (DEBUG) std::cout << format("in upgrade time {}, early stop after {} elements, all is {}.\n", i, for_count, dp_cell[i].size(), status_score, current_score_bar);
                    break; // all below bar, no need to calc
                }
                if (i == upgrade_time) {
                    // full upgraded
                    success_rate = 1;
                    e_gain = gain;
                    e_df_cost = SUCCESS_DOGFOOD_COST;
                    e_score_gain = status_score - SCORE_BAR;
                    if (DEBUG)
                        std::cout << format("DP {}: {} {} C:{} SS:{} EG:{} EDF:{} SR:{}, ESG:{}\n",
                            i, status2str(status), e_gain > DOGFOOD_LOSS[current_upgrade + i] ? "SUCC" : "FAIL", count, status_score, e_gain, e_df_cost, success_rate, e_score_gain);

                    dp_map[i][status] = {
                        count,
                        status_score,
                        e_gain,
                        e_df_cost,
                        success_rate,
                        e_score_gain
                    };
                }
                else {
                    // partial upgraded, need DP
                    auto current_base = 1;
                    auto route_number = DATA::AFFIX_NUM * (1 + DATA::AFFIX_UPDATE_MAX
                        - DATA::AFFIX_UPDATE_MIN);
                    for (int a_idx = 0; a_idx < DATA::AFFIX_NUM; a_idx++) {
                        for (int upd_w = DATA::AFFIX_UPDATE_MIN; upd_w <= DATA::AFFIX_UPDATE_MAX; upd_w++) {
                            auto new_status = status + upd_w * current_base;
                            auto target = dp_map[i + 1].find(new_status);
                            if (target == dp_map[i + 1].end()) {
                                // not in map, default not needed
                                e_gain += DOGFOOD_LOSS[current_upgrade + i + 1];
                                e_df_cost -= DOGFOOD_LOSS[current_upgrade + i + 1];
                            }
                            else {
                                auto& [t_count, t_status_score, t_e_gain,
                                    t_e_df_cost, t_success_rate, t_e_score_gain]
                                    = target->second;
                                e_gain += t_e_gain;
                                e_df_cost += t_e_df_cost;
                                success_rate += t_success_rate;
                                e_score_gain += t_success_rate * t_e_score_gain;
                            }
                            // if (DEBUG) std::cout << format("{} {} {} {} {} {}\n", a_idx, upd_w, e_gain, e_df_cost, success_rate, e_score_gain);
                        }
                        current_base *= BASE;
                    }
                    e_gain /= route_number;
                    e_df_cost /= route_number;
                    success_rate /= route_number;
                    if (success_rate > 0) e_score_gain /= route_number * success_rate;
                    if (DEBUG) std::cout << format("DP {}: {} {} C:{} SS:{} EG:{} EDF:{} SR:{}, ESG:{}\n",
                        i, status2str(status), e_gain > DOGFOOD_LOSS[current_upgrade + i] ? "SUCC" : "FAIL", count, status_score, e_gain, e_df_cost, success_rate, e_score_gain);
                    if (e_gain > DOGFOOD_LOSS[current_upgrade + i])
                        dp_map[i][status] = {
                            count,
                            status_score,
                            e_gain,
                            e_df_cost,
                            success_rate,
                            e_score_gain
                    };
                }
            }
        }
        if (dp_map[0].find(0) == dp_map[0].end()) {
            dftype gain = DOGFOOD_LOSS[current_upgrade];
            dftype df_cost = -gain;
            double s_rate = 0;
            stype score_gain = 0;
            return std::make_tuple(
                false,
                gain,
                df_cost,
                s_rate,
                score_gain * 1. / SCORE_MULTIPLIER
            );
        }
        auto& [count, status_score, e_gain, e_df_cost, success_rate,
            e_score_gain] = dp_map[0][0];
        return std::make_tuple(
            true,
            e_gain,
            e_df_cost,
            success_rate,
            e_score_gain * 1. / SCORE_MULTIPLIER
        );
    }

    auto calc(const DATA::Artifact& art, const std::vector<double>& score,
        double score_bar, dftype gain) {
        std::vector<int> weight;
        for (auto& [t, w] : art.sub)
            weight.push_back(w);
        // freopen("r1.txt", "w", stdout);
        auto res = calc(weight, score, N - art.level, score_bar, gain);
        // freopen("r2.txt", "w", stdout);
        // auto res2 = calc2(weight, score, N - art.level, score_bar, gain);
        // fflush(stdout);
        // if (res != res2)
        //     throw std::runtime_error("two res not equal");
        return res;
    }

    auto select_sub_score(const DATA::Artifact& art, const std::map<DATA::AFFIX_NAMES, double>& sub_scores) {
        std::vector<double> res;
        for (auto& [t, w] : art.sub) {
            auto ite = sub_scores.find(t);
            if (ite == sub_scores.end())
                throw std::runtime_error("sub not found in sub_scores");
            res.push_back(ite->second);
        }
        return res;
    }

    // recommended calling version, have 3-sub support.
    std::tuple<bool, DP::dftype, DP::dftype, double, double> calc(const DATA::Artifact& art,
        const std::map<DATA::AFFIX_NAMES, double>& sub_scores, double score_bar, dftype gain) {

        if (art.sub.size() == 3) {
            if (art.level != 0)
                throw std::runtime_error("input 3 sub but level not zero artifact");
            auto current_art = art;
            current_art.level++;
            std::vector<DATA::AFFIX_NAMES> current_sub;
            for (auto& [t, w] : art.sub)
                current_sub.push_back(t);
            auto sub_dist = DATA::get_sub_distribution(art.main, current_sub);
            auto sub_weight_sum = DATA::weighted_sum(sub_dist) * (DATA::AFFIX_UPDATE_MAX - DATA::AFFIX_UPDATE_MIN + 1);

            dftype e_gain = 0, e_df_cost = 0;
            double success_rate = 0, e_score_gain = 0;
            for (auto& [t, w] : sub_dist) {
                for (int i = DATA::AFFIX_UPDATE_MIN; i <= DATA::AFFIX_UPDATE_MAX; i++) {
                    current_art.sub.push_back({ t, i });
                    auto ores = calc(current_art, sub_scores, score_bar, gain);
                    auto& [t_success, t_e_gain, t_e_df_cost, t_success_rate, t_e_score_gain] = ores;
                    e_gain += t_e_gain * w;
                    e_df_cost += t_e_df_cost * w;
                    success_rate += t_success_rate * w;
                    e_score_gain += t_success_rate * t_e_score_gain * w;
                    // std::cout << format("w {} upgrade? {} expected gain {} dogfood cost {} success rate above bar {} expected better score if success {}\n", w, t_success, t_e_gain, t_e_df_cost, t_success_rate, t_e_score_gain);
                    current_art.sub.pop_back();
                }
            }
            e_gain /= sub_weight_sum;
            e_df_cost /= sub_weight_sum;
            success_rate /= sub_weight_sum;
            if (success_rate > 0) e_score_gain /= sub_weight_sum * success_rate;
            bool success = e_gain > DOGFOOD_LOSS[0];
            if (!success) {
                e_gain = DOGFOOD_LOSS[0];
                e_df_cost = -e_gain;
                success_rate = 0;
                e_score_gain = 0;
            }
            return std::make_tuple(
                success,
                e_gain,
                e_df_cost,
                success_rate,
                e_score_gain
            );
        }
        return calc(art, select_sub_score(art, sub_scores), score_bar, gain);
    }

    // get artifact string as input
    // std::tuple<bool, DP::dftype, DP::dftype, double, double> calc(const std::string &art_string,
    //     const std::map<DATA::AFFIX_NAMES, double>& sub_scores, double score_bar, dftype gain) {
    //     auto art = DATA::Artifact(art_string);
    //     return calc(art, sub_scores, score_bar, gain);
    // }

    // output cell data into yaml. key1=N, key2=second, [list of first]
    void output_yaml() {
        init();
        // std::vector<std::map<int, int>> res;
        for (int n = 1; n <= N; n++) {
            // std::cout << n << ' ' << m.size() << ' ' << (1 << (4 * n)) 
            //           << std::endl;
            // for (auto i : m)
            //     std::cout << i.first << ' ' << i.second << std::endl;
            std::map<int, std::vector<int>> mm;
            for (auto& [status, count] : cell[n])
                mm[count] = std::vector<int>();
            for (auto& [status, count] : cell[n])
                mm[count].push_back(status);
            mm[0] = { 1 << (DATA::AFFIX_NUM * n) }; // here save enumerate number
            std::cout << n << ": \n";
            for (auto& [status, count] : mm) {
                std::cout << "  " << status << ": [ ";
                for (auto j : count)
                    std::cout << j << ',';
                std::cout << "]\n";
            }
        }
    }

    void test_one_artifact(bool output_result = true, bool debug = false) {
        DEBUG = debug;
        std::vector<double> s = { 0, 0, 1, 1 };
        std::vector<int> w = { 7, 9, 9, 10 };
        int upgrade_time = 5;
        dftype input_gain = 100000;
        double score_bar = 51;
        auto r = calc(w, s, upgrade_time, score_bar, input_gain);
        auto& [do_upgrade, gain, dogfood_cost, success_rate, score_gain] = r;
        if (output_result) {
            std::cout << format("upgrade? {}\nexpected gain {}\ndogfood cost {}\nsuccess rate above bar {}\nexpected better score if success {}\n", do_upgrade, gain, dogfood_cost, success_rate, score_gain);
            std::cout << do_upgrade << ' ' << gain << ' ' << dogfood_cost << ' '
                << success_rate << ' ' << score_gain << std::endl;
        }
        // std::cout << r.do_upgrade << ' ' << r.dogfood_cost << ' ' 
        //           << r.success_rate << ' ' << r.score_gain << std::endl;
    }

    auto test_sub_score(const std::map<DATA::AFFIX_NAMES, double>& sub_scores, double score_bar, dftype gain, double randnum = -1) {
        if (randnum < 0)
            randnum = DATA::rand();
        DATA::Artifact art = DATA::get_drop(randnum);
        return calc(art, sub_scores, score_bar, gain);
    }

    bool FIND_GAIN_DEBUG = false;

    dftype get_expected_dfcost(const std::map<DATA::AFFIX_NAMES, double>& sub_scores, double score_bar, const std::vector<std::pair<DATA::Artifact, double>>& allart, dftype gain) {
        std::vector<std::vector<std::pair<double, double>>> results;
        results.resize(allart.size());
        // double results = 0;
#pragma omp parallel for
        for (int i = 0; i < allart.size(); i++) {
            // TODO enumerate sub weight
            auto [art, rate] = allart[i];
            for (auto i = art.sub.size(); i--; ) rate /= DATA::AFFIX_UPDATE_MAX - DATA::AFFIX_UPDATE_MIN + 1;
            while (1) {
                bool addflag = false;
                for (int i = 0; i < art.sub.size(); i++)
                    if (art.sub[i].second == DATA::AFFIX_UPDATE_MAX) art.sub[i].second = DATA::AFFIX_UPDATE_MIN;
                    else {
                        art.sub[i].second++;
                        addflag = true;
                        break;
                    }
                if (!addflag) break;
                auto [success, e_gain, e_df_cost, success_rate, e_score_gain] = calc(art, sub_scores, score_bar, gain);
                results[i].push_back({ e_df_cost, rate });
            }
            if (FIND_GAIN_DEBUG && i % 10 == 0) std::cout << "art number " << i << '/' << allart.size() << "\r";
            // std::cout << format("{} {} {} {} {} {}\n", rate, success, e_gain, e_df_cost, success_rate, e_score_gain);
        }
        double final_result = 0;
        for (auto& result : results)
            for (auto& [i, j] : result)
                final_result += i * j;
        if (FIND_GAIN_DEBUG) std::cout << "gain " << gain << " exp_df_cost " << final_result << std::endl;
        return final_result;
    }

    // 变量：score bar, score map, set (including all set), dfcost。目标：找到给定dfcost的gain设置
    // max_gain 最大可能价值，gain_accuracy二分到什么精度。一般不需要动
    dftype find_gain(const std::map<DATA::AFFIX_NAMES, double>& sub_scores, double score_bar, dftype dfcost, DATA::SET_NAMES set = DATA::SET_NAMES::end,
        dftype max_gain = 100000000, dftype gain_precision = 1) {
        // const std::vector<std::pair<DATA::Artifact, double>> &allart = set == DATA::SET_NAMES::end ? DATA::all_artifacts_accumulated : DATA::all_artifacts_accumulated_divided_by_set[set];
        auto allart = DATA::get_all_artifacts_with_probs(set);
        dftype min_gain = -SUCCESS_DOGFOOD_COST;
        // result drops in [min_gain, max_gain)
        while (max_gain - min_gain > gain_precision) {
            auto mid = (max_gain + min_gain) / 2;
            if (FIND_GAIN_DEBUG) std::cout << "current L M R " << min_gain << ' ' << mid << ' ' << max_gain << std::endl;
            if (get_expected_dfcost(sub_scores, score_bar, allart, mid) > dfcost)  max_gain = mid;
            else min_gain = mid;
        }
        return (max_gain + min_gain) / 2;
    }

    /*
    generate random input for find_gain. if no input, all random generate; otherwise use input as output.
    for sub scores, for every sub, 50% is 0, 50% is uniform random 0-1. specially, number atk/hp/def is randomized in 0-0.5 and multiplies atkp/hpp/defp.
    for score_bar, normal distribution (30, 15) and re-generate when result not in [0, 60]. larger than 60 almost always get infinite gain, although
        theoritically maximum score is 90.
    for dfcost, one round get dogfood 10080 except 5x (which is counted in calc-dfcost). there's not much event to gain artifact dogfood,
        stable gain method contains daily overworld artifact farming (approx. 60000 exp per day) and from serenitea pot (100000 per week).
        compared with whole week stamina to exp (10800 * 9 * 7) is about 10%. there's also 10% chance to gain 2x/5x exp when upgrading,
        so choose range as (10000, 14000). lower because may use 5x 3 to 1.
    for set, uniformly choose from five possible set.
    */
    std::tuple<
        std::map<DATA::AFFIX_NAMES, double>,
        double,
        dftype,
        DATA::SET_NAMES
    > generate_random_gain_input(std::map<DATA::AFFIX_NAMES, double> sub_scores = std::map<DATA::AFFIX_NAMES, double>(), double score_bar = -1,
        dftype dfcost = -1, DATA::SET_NAMES set = DATA::SET_NAMES::end) {
        // sub_scores
        if (sub_scores.size() == 0) {
            const std::vector<DATA::AFFIX_NAMES> random_affix = { DATA::AFFIX_NAMES::hpp, DATA::AFFIX_NAMES::atkp, DATA::AFFIX_NAMES::defp, DATA::AFFIX_NAMES::em, DATA::AFFIX_NAMES::er, DATA::AFFIX_NAMES::cr, DATA::AFFIX_NAMES::cd };
            const std::vector<std::pair<DATA::AFFIX_NAMES, DATA::AFFIX_NAMES>> number_affix = {
                { DATA::AFFIX_NAMES::hp, DATA::AFFIX_NAMES::hpp, },
                { DATA::AFFIX_NAMES::atk, DATA::AFFIX_NAMES::atkp, },
                { DATA::AFFIX_NAMES::def, DATA::AFFIX_NAMES::defp, },
            };
            const double number_affix_multiplier = 0.5;
            double max = 1e-10;
            for (auto& affix : random_affix) {
                double r = DATA::rand();
                if (r < 0.5) r = 0;
                else r = DATA::rand();
                sub_scores[affix] = r;
                if (r > max) max = r;
            }
            for (auto& [affix, weight] : sub_scores)
                weight /= max;
            for (auto& [number, percent] : number_affix)
                sub_scores[number] = sub_scores[percent] * number_affix_multiplier * DATA::rand();
        }

        // score_bar
        while (score_bar < 0 || score_bar > 60) {
            score_bar = DATA::rand_normal_distribution(30, 15);
        }

        // dfcost
        if (dfcost == -1)
            dfcost = DATA::randint(4000) + 10000;

        // set
        if (set == DATA::SET_NAMES::end)
            set = DATA::get_random_set();

        return { sub_scores, score_bar, dfcost, set };
    }

    auto read_existing_weight(const std::string filename) {
        std::map<std::string, std::map<DATA::AFFIX_NAMES, double>> sub_scores;
        std::vector<std::string> order = {
            "hp",
            "atk",
            "def",
            "hpp",
            "atkp",
            "defp",
            "em",
            "er",
            "cr",
            "cd"
        };
        // FILE *f = fopen(filename.c_str(), "r");
        std::ifstream input(filename, std::ios::in);
        if (input.fail())
            return sub_scores;
        while (1) {
            std::map<DATA::AFFIX_NAMES, double> m;
            std::string note;
            input >> note;
            if (input.eof())
                break;
            for (auto& i : order) {
                double num;
                input >> num;
                m[DATA::string_to_affix_names.find(i)->second] = num;
            }
            m[DATA::AFFIX_NAMES::er] = DATA::rand(); // recharge can be any weight
            sub_scores[note] = std::move(m);
        }
        return sub_scores;
    }

}

#ifdef __clang__ // clang is compiled to JS, define interface

EMSCRIPTEN_BINDINGS(my_module) {
    function("find_gain", &DP::find_gain);
    auto (&choose_calc)(const DATA::Artifact&, const std::map<DATA::AFFIX_NAMES, double> &, double, DP::dftype) = DP::calc;
    function("calc", &choose_calc);
}

#else
int main() {
    // OMP_THREADS_MAX omp_set_num_threads(1024);
    // DP::output_yaml();
    DP::test_one_artifact(false);
    /*
    int current = clock();
    for (int i = 0; i < 10; i++)
        DP::test_one_artifact(false);
    std::cout << format("used time {}\n", (clock() - current) * 1.0 / CLOCKS_PER_SEC);
    */

    // check appear rate calculation
    // for (int i = 0; i < 100; i++) {
    //     // auto art = DATA::random_one_artifact(DATA::SET_NAMES::goblet, DATA::AFFIX_NAMES::end, 0, { {DATA::AFFIX_NAMES::cr, 8 } });
    //     auto art = DATA::random_one_artifact();
    //     std::cout << art.to_string() <<  '\n' << DATA::artifact_appear_rate(art, true) << std::endl;
    // }

    auto cc = clock();
    auto all_arts = DATA::get_all_artifacts_with_probs();
    std::cout << "all arts with probs size " << all_arts.size() << " time " << (clock() - cc) * 1.0 / CLOCKS_PER_SEC << std::endl;
    for (int i = static_cast<int>(DATA::SET_NAMES::start) + 1; i < static_cast<int>(DATA::SET_NAMES::end); i++) {
        auto set = static_cast<DATA::SET_NAMES>(i);
        auto set_arts = DATA::get_all_artifacts_with_probs(set);
        std::cout << DATA::type_to_string(DATA::string_to_set_names, set) << " arts with prob size " << set_arts.size() << std::endl;
    }
    // FILE* f = fopen("arts.out", "w");
    // for (auto& i : all_arts)
    //     fprintf(f, "%.15f|%s\n", i.second, i.first.to_string().c_str());

    // check appear rate and print
    // int times = 4, initial = 1000000;
    // auto m = DATA::check_sub_appear_rate(times, initial, DATA::SET_NAMES::flower);
    // for (auto& [name, weight] : m) {
    // 	std::cout << DATA::type_to_string(DATA::string_to_affix_names, name) << ' ' << weight * 1. / times / initial << '\n';
    // }

    // test random drop time cost. sub score is hutao's
    std::map<DATA::AFFIX_NAMES, double> sub_scores = {
        { DATA::AFFIX_NAMES::atk, 0.14 },
        { DATA::AFFIX_NAMES::hp, 0.16 },
        { DATA::AFFIX_NAMES::def, 0 },
        { DATA::AFFIX_NAMES::atkp, 0.29 },
        { DATA::AFFIX_NAMES::hpp, 0.49 },
        { DATA::AFFIX_NAMES::defp, 0 },
        { DATA::AFFIX_NAMES::em, 1 },
        { DATA::AFFIX_NAMES::er, 0 },
        { DATA::AFFIX_NAMES::cr, 0.92 },
        { DATA::AFFIX_NAMES::cd, 0.72 },
    };
    double score_bar = 40;
    DP::dftype gain = 1000000;
    /*
    cc = clock();
    // # pragma omp parallel for
    for (int i = 0; i < 10000; i++) {
        // DP::DEBUG = true;
        auto randnum = DATA::rand();
        auto art = DATA::get_drop(randnum);
        // std::cout << art.to_string() << ' ' << format("{:.12f}", randnum) << '\n';
        auto res = DP::test_sub_score(sub_scores, score_bar, gain, randnum);
        auto& [do_upgrade, gain, dogfood_cost, success_rate, score_gain] = res;
        // std::cout << format("upgrade? {} expected gain {} dogfood cost {} success rate above bar {} expected better score if success {}\n", do_upgrade, gain, dogfood_cost, success_rate, score_gain);
    }
    std::cout << (clock() - cc) * 1.0 / CLOCKS_PER_SEC << '\n';
    */

    /*
    test five set gain find.
    */
    /*
    // DP::DEBUG = true;
    for (auto& [name, set] : DATA::string_to_set_names) {
        std::cout << "test set " << name << std::endl;
        auto start_time = clock();
        DP::find_gain(sub_scores, score_bar, 15000, set);
        std::cout << "test over, time " << (clock() - start_time) * 1.0 / CLOCKS_PER_SEC << std::endl;
    }
    */

    // check artifact to str and str to artifact works
    for (int i = 0; i < 10000; i++) {
        // DP::DEBUG = true;
        auto randnum = DATA::rand();
        auto art = DATA::get_drop(randnum);
        auto art_str = art.to_string();
        DATA::Artifact new_art(art_str);
        auto new_art_str = new_art.to_string();
        if (art_str != new_art_str) {
            std::cout << art_str << '\n' << new_art.to_string() << std::endl;
            throw std::runtime_error(art_str);
        }
        auto [ss, bar, df, set] = DP::generate_random_gain_input();
        df = 2100000000;
        auto try1 = DP::calc(art_str, ss, bar, df);
        auto &[success, gain, dfcost, success_rate, scoregain] = try1;
        auto try2 = DP::calc(art, ss, bar, df);
        auto &[success2, gain2, dfcost3, success_rate2, scoregain2] = try2;
        std::cout << format("success:{} gain:{} dfcost:{} success_rate:{} scoregain:{}\n", success, gain, dfcost, success_rate, scoregain);
        if (try1 != try2) {
			std::cout << format("success:{} gain:{} dfcost:{} success_rate:{} scoregain:{}\n", success2, gain2, dfcost2, success_rate2, scoregain2);
            throw std::runtime_error("");
        }
    }
    return 0;

    // read predefined weights, random bar/df/set and calculate results
    // auto res = DP::read_existing_weight("weights.txt");
    // for (auto &[note, data] : res) {
    //     std::cout << note << ' ';
    //     for (auto &[affix, wei] : data) {
    //         std::cout << format("{}:{} ", type_to_string(DATA::string_to_affix_names, affix), wei);
    //     }
    //     std::cout << std::endl;
    // }
    // std::cout << res.size() << std::endl;
    // return 0;
    /*
    // auto res = DP::read_existing_weight("weights.txt");
    for (auto& [note, data] : res) {
        auto [ss, bar, df, set] = DP::generate_random_gain_input(data);
        auto result = DP::find_gain(ss, bar, df, set);
        std::string s = "";
        for (auto& [affix, w] : DATA::SUB_PROB_WEIGHT) {
            auto name = DATA::type_to_string(DATA::string_to_affix_names, affix);
            s += format("{}:{} ", name, ss[affix]);
        }
        s += format(" bar:{} cost:{} set:{} result:{}", bar, df, DATA::type_to_string(DATA::string_to_set_names, set), result);
        std::cout << s << std::endl;
    }
    return 0;
    */

    /*
    // test random generate input and calculate result
    // DP::FIND_GAIN_DEBUG = true;
    for (auto i = 1000; i--; ) {
        auto [ss, bar, df, set] = DP::generate_random_gain_input();
        auto result = DP::find_gain(ss, bar, df, set);
        std::string s = "";
        for (auto& [affix, w] : DATA::SUB_PROB_WEIGHT) {
            auto name = DATA::type_to_string(DATA::string_to_affix_names, affix);
            s += format("{}:{} ", name, ss[affix]);
        }
        s += format(" bar:{} cost:{} set:{} result:{}", bar, df, DATA::type_to_string(DATA::string_to_set_names, set), result);
        std::cout << s << std::endl;
    }
    */

    /*
    // read generated data and check data cost and real cost
    std::string result_file = "result.txt";
    std::ifstream rin(result_file);
    while (!rin.fail()) {
        char s[1024];
        rin.getline(s, 1024);
        std::string line = s;
        std::map<DATA::AFFIX_NAMES, double> sub_scores;
        double bar, cost, result, nnresult;
        DATA::SET_NAMES set;
        std::istringstream sin(line);
        for (int _ = 15; _--; ) {
            std::string ones;
            sin >> ones;
            auto comma = ones.find(":");
            auto name = ones.substr(0, comma), value = ones.substr(comma + 1);
            if (name == "bar") bar = std::stod(value);
            else if (name == "cost") cost = std::stod(value);
            else if (name == "set") set = DATA::string_to_set_names.find(value)->second;
            else if (name == "result") result = std::stod(value);
            else if (name == "nnresult") nnresult = std::stod(value);
            else sub_scores[DATA::string_to_affix_names.find(name)->second] = std::stod(value);
        }
        if (cost > 14000) continue;
        auto calc_cost = DP::get_expected_dfcost(sub_scores, bar, DATA::get_all_artifacts_with_probs(set), nnresult);
        std::cout << line << "real_cost:" << calc_cost << " percent:" << calc_cost / cost << std::endl;
    }
    */
}
#endif

