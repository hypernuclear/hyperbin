qt_add_executable(tst_website_links
    tst_website_links.cpp
)

target_link_libraries(tst_website_links PRIVATE test_common Qt6::Test Qt6::Core)

add_test(NAME Links.tagsReplaceOldOnesAndKeepTheRest
         COMMAND tst_website_links tags_replaceOldOnesAndKeepTheRest)
add_test(NAME Links.tagsResolveAPathAgainstTheSite
         COMMAND tst_website_links tags_resolveAPathAgainstTheSite)
add_test(NAME Links.tagsLeaveAnythingPrivateAlone
         COMMAND tst_website_links tags_leaveAnythingPrivateAlone)
add_test(NAME Links.tagsNeedACampaign
         COMMAND tst_website_links tags_needACampaign)

# Windows: put Qt's DLLs on PATH for these tests. Must be called here,
# in the same directory scope as the add_test calls above.
hyperbin_fix_test_path()
