// ReSharper disable CppParameterNeverUsed
// ReSharper disable CppDeclaratorNeverUsed
// ReSharper disable CppAssignedValueIsNeverUsed
// ReSharper disable CppEntityAssignedButNoRead

#include "entt/entt.hpp"

#include <any>
#include <cassert>
#include <format>
#include <iostream>
#include <span>
#include <typeindex>
#include <unordered_map>

struct position {
  int x;
  int y;

  static void on_destroy(const entt::registry &registry, entt::entity entity) { }

  static void on_update(const entt::registry &registry, entt::entity entity) { }

  static void on_construct(const entt::registry &registry, entt::entity entity) {
    const auto &position_added = registry.get<position>(entity);
  }
};

struct velocity {
  int dx, dy;
};

struct hp {
  int cur_val;
  int max_val;
};

static void example();
static void details();

int main() {
  example();
  details();
}

static void example() {
  std::cout << " --- example --- " << std::endl;
  static auto print_entity = [](entt::entity the_entity) {
    using traits = entt::entt_traits<entt::entity>;
    auto version = traits::to_version(the_entity);
    auto entity_id = traits::to_entity(the_entity);
    auto entity_combined_integer = traits::to_integral(the_entity);
    std::format_to(std::ostream_iterator<char>{std::cout},
                   "entity 0x{0:08x}: version: {1}; id: {2:>2d}\n",
                   entity_combined_integer,
                   version,
                   entity_id);
    std::cout.flush();
  };
#pragma region "创建 registry (context)"
  entt::registry registry;
#pragma endregion

#pragma region "entity 基础操作"
  entt::entity entity0 = registry.create();
  entt::entity entity1 = registry.create();
  entt::entity entity2 = registry.create();
  registry.each([](entt::entity the_entity) {
    print_entity(the_entity);
  });
  registry.destroy(entity2);
  entity2 = registry.create();
  print_entity(entity2);
#pragma endregion

#pragma region "component 基础操作"
  const position &position0 = registry.emplace<position>(entity0, position{1, 2});
  const position &position1 = registry.get<position>(entity0);
  const position &position2 = registry.get_or_emplace<position>(entity0, position{3, 4});
  const position &position3 = registry.replace<position>(entity0, position{5, 6});
  const position &position4 = registry.patch<position>(entity0,
                                                       [](position &pos) {
                                                         pos.x = 7;
                                                         pos.y = 8;
                                                       });
  const position &position5 = registry.emplace_or_replace<position>(entity0, position{9, 10});
  const position *position6 = registry.try_get<position>(entity0);

  entt::connection position_construct_conn = registry.on_construct<position>().connect<&position::on_construct>();
  registry.on_construct<position>().disconnect(position_construct_conn);
  registry.on_update<position>().connect<&position::on_update>();
  registry.on_destroy<position>().connect<&position::on_destroy>();
  std::size_t removed_count = registry.remove<position>(entity0);

  registry.emplace<position>(entity0, position{1, 2});
  registry.emplace<velocity>(entity0, velocity{100, 200});
  registry.emplace<velocity>(entity1, velocity{300, 400});

  entt::view<entt::get_t<velocity>> velocity_view = registry.view<velocity>();
  const velocity &velocity0 = velocity_view.get<velocity>(entity0);
  for (entt::entity entity : velocity_view) { }

  velocity_view.each([](entt::entity entity, const velocity &the_velocity) { });
  for (auto &&[entity, the_velocity] : velocity_view.each()) { }

  entt::view<entt::type_list<velocity, position>> velocity_position_view = registry.view<velocity, position>();
  auto [velocity1, position7] = velocity_position_view.get<velocity, position>(entity0);
  for (auto &&[entity, the_velocity, the_position] : velocity_position_view.each()) { }

  entt::view<entt::type_list<velocity>, entt::type_list<position>>
      velocity_without_position_view = registry.view<velocity>(entt::exclude<position>);
#pragma endregion
}

static void entity_details();
static void component_details();
static void view_details();
static void group_details();

static void details() {
  entity_details();
  component_details();
  view_details();
  group_details();
}

static void entity_details() {
  std::cout << " --- entity details ---" << std::endl;

  entt::registry registry;
  auto print_entities_in_registry = [&registry] {
    auto entity_to_string = [](entt::entity entity) {
      return std::format("{:03x} {:05x}", entt::to_version(entity), entt::to_entity(entity));
    };
    std::span entities_span = {registry.data(), registry.size()};
    entt::entity free_list_head = registry.released();
    std::cout << "free list head: " << entity_to_string(free_list_head) << "; ";
    std::cout << "entities: ";
    for (entt::entity entity : entities_span) {
      std::cout << entity_to_string(entity) << ", ";
    }
    std::cout << std::endl;
  };

  static_assert(!std::is_same_v<entt::entity, std::uint32_t>);
  static_assert(entt::entity{0x001'00001} != entt::tombstone);
  static_assert(entt::entity{0xfff'00001} == entt::tombstone);
  static_assert(entt::entity{0xfff'fffff} == entt::tombstone);
  static_assert(entt::entity{0x000'00001} != entt::null);
  static_assert(entt::entity{0x000'fffff} == entt::null);
  static_assert(entt::entity{0xfff'fffff} == entt::null);

  entt::entity e0, e1, e2, e3, e4;
  e0 = registry.create();
  print_entities_in_registry();
  e1 = registry.create();
  print_entities_in_registry();
  e2 = registry.create();
  print_entities_in_registry();
  e3 = registry.create();
  print_entities_in_registry();
  e4 = registry.create();
  print_entities_in_registry();

  registry.destroy(e0);
  print_entities_in_registry();
  registry.destroy(e2);
  print_entities_in_registry();
  assert(!registry.valid(e2));

  e2 = registry.create();
  print_entities_in_registry();
  e0 = registry.create();
  print_entities_in_registry();
}

static void component_details() {
  std::cout << " --- component details ---" << std::endl;

  entt::registry registry;

#pragma region "component storage 思路解释"
  {
    using entity_type = int;
    std::unordered_map<std::type_index, std::unordered_map<entity_type, std::any>> component_storage_map;
    std::unordered_map<entity_type, std::any> &position_storage = component_storage_map[typeid(position)];
    std::unordered_map<entity_type, std::any> &velocity_storage = component_storage_map[typeid(velocity)];
    entity_type my_entity_id = 0;
    position_storage[my_entity_id] = position{1, 2};
    velocity_storage[my_entity_id] = velocity{3, 4};
  }
#pragma endregion

  entt::sparse_set sparse_set;
  constexpr auto reserve_size = 10;
  sparse_set.reserve(reserve_size);
  auto print_sparse_set = [&sparse_set] {
    std::cout << "set size: " << sparse_set.size() << '\n';

    std::cout << "packed: ";
    std::vector<entt::entity> &packed_ref = sparse_set._get_packed_ref();
    for (entt::entity entity : packed_ref) {
      std::format_to(std::ostream_iterator<char>{std::cout},
                     "{0:>3d}, ",
                     static_cast<std::int32_t>(entt::to_integral(entity)));
    }
    std::cout << "\n";

    std::cout << "sparse: ";
    std::vector<entt::entity *> &sparse_pages = sparse_set._get_sparse_ref();
    if (!sparse_pages.empty()) {
      if (entt::entity *sparse_first_page = sparse_pages.front()) {
        std::span sparse_first_page_span = {sparse_first_page, reserve_size};
        for (entt::entity entity : sparse_first_page_span) {
          std::format_to(std::ostream_iterator<char>{std::cout},
                         "{0:>3d}, ",
                         static_cast<std::int32_t>(entt::to_integral(entity)));
        }
      }
    }
    std::cout << std::endl;
  };
  sparse_set.emplace(entt::entity{4});
  print_sparse_set();

  sparse_set.emplace(entt::entity{6});
  print_sparse_set();

  sparse_set.emplace(entt::entity{2});
  print_sparse_set();

  sparse_set.emplace(entt::entity{0});
  print_sparse_set();

  sparse_set.emplace(entt::entity{8});
  print_sparse_set();

  sparse_set.swap_elements(entt::entity{0}, entt::entity{8});
  print_sparse_set();

  sparse_set.remove(entt::entity{6});
  print_sparse_set();

  std::cout << "sparse set iteration: ";
  for (entt::entity entity : sparse_set) {
    std::cout << entt::to_entity(entity) << ", ";
  }
  std::cout << std::endl;
}

static void view_details() {
  std::cout << " --- view details ---" << std::endl;

  entt::registry registry;
  entt::entity e[5];
  for (entt::entity &entity : e) {
    entity = registry.create();
  }
  for (int i : {0, 1, 2, 3}) {
    entt::entity entity = e[i];
    registry.emplace<position>(entity);
  }
  for (int i : {1, 3, 4}) {
    entt::entity entity = e[i];
    registry.emplace<velocity>(entity);
  }
  for (int i : {1, 2}) {
    entt::entity entity = e[i];
    registry.emplace<hp>(entity, hp{50, 100});
  }

  std::cout << "Entities with position: \n";
  for (auto &&[entity, the_position] : registry.view<position>().each()) {
    std::cout << entt::to_entity(entity) << ", ";
  }
  std::cout << std::endl;

  std::cout << "Entities with position and velocity: \n";
  for (auto &&[entity, the_position, the_velocity] : registry.view<position, velocity>().each()) {
    std::cout << entt::to_entity(entity) << ", ";
  }
  std::cout << std::endl;

  std::cout << "Entities with position and velocity without hp: \n";
  for (auto &&[entity, the_position, the_velocity] : registry.view<position, velocity>(entt::exclude<hp>).each()) {
    std::cout << entt::to_entity(entity) << ", ";
  }
  std::cout << std::endl;
}

static void group_details() {
  std::cout << " --- group details ---" << std::endl;

  entt::registry registry;
  entt::entity e[10];
  for (entt::entity &entity : e) {
    entity = registry.create();
  }
  for (int i : {3, 7, 8, 6}) {
    entt::entity entity = e[i];
    registry.emplace<position>(entity, position{i, 0});
  }
  for (int i : {4, 5}) {
    entt::entity entity = e[i];
    registry.emplace<velocity>(entity, velocity{i, 0});
  }

  entt::group<entt::owned_t<position, velocity>, entt::get_t<>, entt::exclude_t<>>
      position_group = registry.group<position, velocity>(entt::get<>, entt::exclude<>);
  auto print_group = [&registry, &group = position_group] {
    std::cout << "position: ";
    auto &&position_storage = registry.view<position>().storage();
    std::span position_storage_span = {position_storage.data(), position_storage.size()};
    for (auto &&entity : position_storage_span) {
      std::cout << entt::to_integral(entity) << ", ";
    }
    std::cout << "\n";

    std::cout << "velocity: ";
    auto &&velocity_storage = registry.view<velocity>().storage();
    std::span velocity_storage_span = {velocity_storage.data(), velocity_storage.size()};
    for (auto &&entity : velocity_storage_span) {
      std::cout << entt::to_integral(entity) << ", ";
    }
    std::cout << "\n";

    std::cout << "group:    ";
    for (entt::entity entity : group) {
      std::cout << entt::to_integral(entity) << ", ";
    }
    std::cout << std::endl;
  };

  std::cout << "Initial group: \n";
  print_group();

  registry.emplace<velocity>(e[7]);
  std::cout << "e7 add velocity: \n";
  print_group();

  registry.emplace<position>(e[4]);
  std::cout << "e4 add position: \n";
  print_group();

  registry.remove<velocity>(e[7]);
  std::cout << "e7 del velocity: \n";
  print_group();
}
