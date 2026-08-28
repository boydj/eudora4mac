#include "filters/filter_file.hpp"
#include "filters/match_engine.hpp"
#include "filters/regexp.hpp"
#include "test_framework.hpp"

using namespace eudora;

namespace {

const std::string kFilterFile =
    "rule Spam to junk\r"
    "id 42\r"
    "incoming\r"
    "header subject:\r"
    "verb contains\r"
    "value viagra\r"
    "conjunction or\r"
    "header «any header»\r"
    "verb contains\r"
    "value lottery\r"
    "junk 100\r"
    "stop\r"
    "rule Work mail\r"
    "id 43\r"
    "incoming\r"
    "manual\r"
    "header from:\r"
    "verb intersects\r"
    "value boss@corp.example\r"
    "transfer Work\r"
    "copyInstead\r"
    "rule Old raise\r"
    "incoming\r"
    "header x-priority:\r"
    "verb is\r"
    "value 1\r"
    "raise\r";

const std::string kSpamMessage =
    "From spammer@example.net Wed Jun 14 12:36:18 1989\r"
    "From: spammer@example.net\r"
    "Subject: cheap VIAGRA now\r"
    "\r"
    "buy now\r";

const std::string kWorkMessage =
    "From boss@corp.example Wed Jun 14 12:36:18 1989\r"
    "From: The Boss <boss@corp.example>\r"
    "Subject: quarterly numbers\r"
    "\r"
    "please review\r";

} // namespace

TEST_CASE("filter file parses records, terms, actions") {
    const auto filters = parse_filters(kFilterFile);
    CHECK_EQ(filters.size(), 3u);
    if (filters.size() < 3)
        return;

    const auto &spam = filters[0];
    CHECK_EQ(spam.name, "Spam to junk");
    CHECK_EQ(spam.id, 42);
    CHECK(spam.incoming);
    CHECK(!spam.outgoing);
    CHECK_EQ(spam.terms[0].header, "subject:");
    CHECK(spam.terms[0].verb == FilterVerb::Contains);
    CHECK_EQ(spam.terms[0].value, "viagra");
    CHECK(spam.conjunction == FilterConjunction::Or);
    CHECK_EQ(spam.terms[1].value, "lottery");
    CHECK_EQ(spam.actions.size(), 2u);
    CHECK(spam.actions[0].keyword == FilterKeyword::Junk);
    CHECK_EQ(spam.actions[0].value, "100");
    CHECK(spam.actions[1].keyword == FilterKeyword::Stop);

    // copyInstead rewrote the transfer into a copy (filtmng.c:270-273).
    const auto &work = filters[1];
    CHECK(work.manual);
    CHECK_EQ(work.actions.size(), 1u);
    CHECK(work.actions[0].keyword == FilterKeyword::Copy);
    CHECK_EQ(work.actions[0].value, "Work");

    // Obsolete "raise" became priority 7 (ReadFilters prescan).
    const auto &old = filters[2];
    CHECK_EQ(old.actions.size(), 1u);
    CHECK(old.actions[0].keyword == FilterKeyword::Priority);
    CHECK_EQ(old.actions[0].value, "7");
}

TEST_CASE("filter file round trips") {
    const auto filters = parse_filters(kFilterFile);
    const std::string text = serialize_filters(filters);
    const auto again = parse_filters(text);
    CHECK_EQ(again.size(), filters.size());
    for (std::size_t i = 0; i < filters.size() && i < again.size(); ++i) {
        CHECK_EQ(again[i].name, filters[i].name);
        CHECK_EQ(again[i].actions.size(), filters[i].actions.size());
        CHECK(again[i].terms[0].verb == filters[i].terms[0].verb);
        CHECK_EQ(again[i].terms[0].value, filters[i].terms[0].value);
    }
    // A filter with no id got one assigned on write.
    CHECK(again.size() == 3 && again[2].id != 0);
}

TEST_CASE("term matching verbs") {
    FilterContext ctx;
    ctx.raw_message = kSpamMessage;

    FilterTerm t;
    t.header = "subject:";
    t.value = "viagra";
    t.verb = FilterVerb::Contains;
    CHECK(term_matches(t, ctx)); // case-insensitive

    t.verb = FilterVerb::NotContains;
    CHECK(!term_matches(t, ctx));

    t.verb = FilterVerb::Is;
    t.value = "cheap VIAGRA now";
    CHECK(term_matches(t, ctx));

    t.verb = FilterVerb::Starts;
    t.value = "cheap";
    CHECK(term_matches(t, ctx));

    t.verb = FilterVerb::Ends;
    t.value = "now";
    CHECK(term_matches(t, ctx));

    t.verb = FilterVerb::Appears;
    CHECK(term_matches(t, ctx));
    t.header = "x-mailer:";
    CHECK(!term_matches(t, ctx));
    t.verb = FilterVerb::NotAppears;
    CHECK(term_matches(t, ctx));

    // Body term.
    FilterTerm b;
    b.header = "«body»";
    b.value = "buy now";
    b.verb = FilterVerb::Contains;
    CHECK(term_matches(b, ctx));
    b.value = "sell later";
    CHECK(!term_matches(b, ctx));

    // Regex.
    FilterTerm r;
    r.header = "subject:";
    r.verb = FilterVerb::Regex;
    r.value = "V[Ii1]AGRA";
    CHECK(term_matches(r, ctx));
    // The compiled regex is cached per-term (as the legacy mt->regex was),
    // so a changed pattern needs a fresh term.
    FilterTerm r2;
    r2.header = "subject:";
    r2.verb = FilterVerb::Regex;
    r2.value = "^quarterly";
    CHECK(!term_matches(r2, ctx));

    // Intersects on addresses.
    FilterContext wctx;
    wctx.raw_message = kWorkMessage;
    FilterTerm ix;
    ix.header = "from:";
    ix.verb = FilterVerb::Intersects;
    ix.value = "boss@corp.example, other@x.y";
    CHECK(term_matches(ix, wctx));
    ix.value = "nobody@nowhere.example";
    CHECK(!term_matches(ix, wctx));

    // intersectsFile with an address-book hook.
    FilterTerm ixf;
    ixf.header = "from:";
    ixf.verb = FilterVerb::IntersectsFile;
    wctx.address_in_book = [](std::string_view addr, std::string_view) {
        return addr == "boss@corp.example";
    };
    CHECK(term_matches(ixf, wctx));

    // Junk score meta term.
    MessageSummary sum;
    sum.spam_score = 80;
    FilterContext jctx;
    jctx.raw_message = kSpamMessage;
    jctx.summary = &sum;
    FilterTerm j;
    j.header = "«junk score»";
    j.verb = FilterVerb::JunkMore;
    j.value = "50";
    CHECK(term_matches(j, jctx));
    j.verb = FilterVerb::JunkLess;
    CHECK(!term_matches(j, jctx));
}

TEST_CASE("conjunctions and run_filters ordering") {
    const auto filters = parse_filters(kFilterFile);

    FilterContext spam_ctx;
    spam_ctx.raw_message = kSpamMessage;
    FilterContext work_ctx;
    work_ctx.raw_message = kWorkMessage;

    // "or" conjunction: subject hit suffices.
    CHECK(filter_matches(filters[0], spam_ctx));
    CHECK(!filter_matches(filters[0], work_ctx));

    const auto fired = run_filters(filters, FilterEvent::Incoming, spam_ctx);
    // junk (pass 4) then stop (pass 9); the work filter never ran.
    CHECK_EQ(fired.size(), 2u);
    if (fired.size() == 2) {
        CHECK(fired[0].action.keyword == FilterKeyword::Junk);
        CHECK(fired[1].action.keyword == FilterKeyword::Stop);
    }

    const auto fired_work = run_filters(filters, FilterEvent::Incoming, work_ctx);
    CHECK_EQ(fired_work.size(), 1u);
    if (!fired_work.empty())
        CHECK(fired_work[0].action.keyword == FilterKeyword::Copy);

    // Manual pass: only the work filter opts in.
    const auto fired_manual = run_filters(filters, FilterEvent::Manual, work_ctx);
    CHECK_EQ(fired_manual.size(), 1u);

    // "unless" conjunction.
    Filter unless = filters[0];
    unless.conjunction = FilterConjunction::Unless;
    unless.terms[1].header = "from:";
    unless.terms[1].verb = FilterVerb::Contains;
    unless.terms[1].value = "spammer";
    CHECK(!filter_matches(unless, spam_ctx)); // exception matched: no fire
}

TEST_CASE("regexp wrapper") {
    auto re = Regexp::compile("ab+c");
    CHECK(re != nullptr);
    if (re) {
        auto pos = re->search("xxabbbc yy");
        CHECK(pos.has_value());
        if (pos)
            CHECK_EQ(*pos, 2u);
        CHECK(!re->search("nothing").has_value());
        CHECK(re->search("abc abc", 3).has_value());
    }
    CHECK(Regexp::compile("([bad") == nullptr);
}

EUTEST_MAIN
