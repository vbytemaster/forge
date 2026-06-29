import forge.xml;

int main() {
   auto doc = forge::xml::document{.root = forge::xml::element{.name = "Root"}};
   auto written = forge::xml::write_value(doc);
   return written.ok() ? 0 : 1;
}
