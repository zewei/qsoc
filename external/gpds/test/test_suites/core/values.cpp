#include <cstring>
#include "../../test.hpp"

TEST_SUITE("core - values")
{

    TEST_CASE("creation")
    {
        SUBCASE("integer") {
            gpds::value value(45);

            SUBCASE("retrieving integer from value") {
                CHECK_EQ(value.get<int>(), 45);
            }
        }

        SUBCASE("boolean") {
            gpds::value value(false);

            SUBCASE("retrieving boolean from value") {
                CHECK_EQ(value.get<bool>(), false);
            }
        }

        SUBCASE ("float") {
            gpds::value value(13.2f);

            SUBCASE("retrieving floating-point from value") {
                REQUIRE(value.get<float>().value_or(0.0f) == doctest::Approx(13.2f));
            }
        }

        SUBCASE("double") {
            gpds::value value(13.2);

            SUBCASE("retrieving floating-point from value") {
                CHECK_EQ(value.get<double>().value_or(0.0), doctest::Approx(13.2));
            }
        }

        SUBCASE("std::size_t") {
            gpds::value value(1137);

            CHECK_EQ(value.get<std::size_t>(), 1137);
        }

        SUBCASE("string") {
            SUBCASE("std::string") {
                gpds::value value(std::string("Hello, World!"));

                SUBCASE("retrieving string from value") {
                    CHECK_EQ(value.get<std::string>(), "Hello, World!");
                }
            }

            SUBCASE("std::string_view") {
                gpds::value value(std::string_view("Hello, World!"));

                SUBCASE("retrieve string from value") {
                    CHECK_EQ(value.get<std::string>(), "Hello, World!");
                }
            }

            SUBCASE("c-string") {
                gpds::value value("Hello, World!");

                SUBCASE("retrieving string from value") {
                    CHECK_EQ(value.get<std::string>(), "Hello, World!");
                }
            }

            SUBCASE("cdata")
            {
                // Note: The actual CDATA (de)serialization tests are not located in here.
                //       This is a unit test for just the value itself.

                gpds::value v{"Hello World!"};
                CHECK_EQ(v.use_cdata(), false);
                v.set_use_cdata(true);
                CHECK_EQ(v.use_cdata(), true);

                SUBCASE("copy ctor")
                {
                    gpds::value v2{ v };
                    CHECK_EQ(v2.use_cdata(), true);
                }

                SUBCASE("move ctor")
                {
                    gpds::value v2{ std::move(v) };
                    CHECK_EQ(v2.use_cdata(), true);
                }

                SUBCASE("copy assignment")
                {
                    gpds::value v2;
                    CHECK_EQ(v2.use_cdata(), false);
                    v2 = v;
                    CHECK_EQ(v2.use_cdata(), true);
                }

                SUBCASE("move assignment")
                {
                    gpds::value v2;
                    CHECK_EQ(v2.use_cdata(), false);
                    v2 = std::move(v);
                    CHECK_EQ(v2.use_cdata(), true);
                }
            }
        }

        SUBCASE("filesystem path")
        {
            gpds::value v(std::filesystem::path{ "/usr/src" });

            SUBCASE("retrieve string from value") {
                CHECK_EQ(v.get<std::filesystem::path>(), "/usr/src");
            }
        }

        SUBCASE("container") {
            gpds::container* container = new gpds::container;
            container->add_value("name", std::string("John Doe"));
            gpds::value value(container);

            SUBCASE("retrieving value from container") {
                auto ctnr = value.get<gpds::container*>().value_or(nullptr);
                REQUIRE(ctnr);
                CHECK_EQ(ctnr->get_value<std::string>("name"), "John Doe");
            }
        }

        SUBCASE("empty container") {
            gpds::value value;
            value.set(new gpds::container);

            const auto& opt = value.get<gpds::container*>();
            CHECK(opt.has_value());
            CHECK(opt.value_or(nullptr));
            CHECK(opt.value()->empty());
        }
    }

    TEST_CASE("default value")
    {
        gpds::container container;
        std::string str = container.get_value<std::string>("doesn't exist").value_or("default");
        CHECK_EQ(str, "default");
    }

    TEST_CASE("type decay")
    {
        gpds::value v1;
        v1.set<int>(42);

        gpds::value v2;
        {
            gpds::container c;
            c.add_value<bool>("test", true);
            v2.set(c);
        }

        SUBCASE("normal")
        {
            CHECK_EQ(v1.get<int>(), 42);
            CHECK(v2.get<gpds::container*>());
        }

        SUBCASE("const")
        {
            CHECK_EQ(v1.get<const int>(), 42);
            CHECK(v2.get<const gpds::container*>());
        }
    }

}
