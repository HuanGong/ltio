cmake_minimum_required(VERSION 3.5)

SET(COMPONENT_SOURCES
  ./log_metrics/metrics_item.cc
  ./log_metrics/metrics_stash.cc
  ./log_metrics/metrics_container.cc

  ./count_min_sketch/count_min_sketch.cc

  ./bloomfilter/bloom_filter.cc

  ./source_loader/source.cc
  ./source_loader/parser/parser.cc
  ./source_loader/loader/loader_factory.cc
  ./source_loader/loader/file_loader.cc
  ./source_loader/parser/column_parser.cc
  ./source_loader/parser/column_parser.cc

  #inverted_indexer
  ./inverted_indexer/indexer.cc
  ./inverted_indexer/expression.cc
  ./inverted_indexer/bitmap_merger.cc
  ./inverted_indexer/posting_field/field.cc
  ./inverted_indexer/posting_field/range_field.cc
  ./inverted_indexer/posting_field/general_str_field.cc
  ./inverted_indexer/posting_field/bitmap_posting_list.cc
  ./inverted_indexer/posting_field/posting_list_manager.cc

  #boolean indexer
  ./boolean_indexer/document.cc
  ./boolean_indexer/be_indexer.cc
  ./boolean_indexer/id_generator.cc
  ./boolean_indexer/posting_list.cc
  ./boolean_indexer/index_scanner.cc
  ./boolean_indexer/scanner_cursor.cc
  ./boolean_indexer/builder/be_indexer_builder.cc
)

add_library(ltcomponent_objs OBJECT ${COMPONENT_SOURCES})
ltio_default_properties(ltcomponent_objs)
