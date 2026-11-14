#include <array>
#include <clap/clap.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
static void require(bool b, const char *message) {
  if (!b) throw std::runtime_error(message);
}
int main(int argc, char **argv) {
  try {
    require(argc == 2, "Supply the path to the built CLAP");
#ifdef _WIN32
    auto library = LoadLibraryA(argv[1]);
    require(library != nullptr, "LoadLibrary failed");
    auto *entry =
        reinterpret_cast<const clap_plugin_entry_t *>(GetProcAddress(library, "clap_entry"));
#else
    auto *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) throw std::runtime_error(dlerror());
    auto *entry = static_cast<const clap_plugin_entry_t *>(dlsym(library, "clap_entry"));
#endif
    require(entry && entry->init(argv[1]), "CLAP entry init");
    auto *factory =
        static_cast<const clap_plugin_factory_t *>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    require(factory && factory->get_plugin_count(factory) == 1, "CLAP factory");
    const auto *description = factory->get_plugin_descriptor(factory, 0);
    std::cout << description->name << " " << description->version << '\n';
    clap_host_t host{};
    host.clap_version = CLAP_VERSION;
    host.name = "Chord smoke test";
    host.vendor = "sca2code";
    host.url = "";
    host.version = "1";
    host.get_extension = [](const clap_host_t *, const char *) -> const void * { return nullptr; };
    host.request_restart = [](const clap_host_t *) {};
    host.request_process = [](const clap_host_t *) {};
    host.request_callback = [](const clap_host_t *) {};
    auto *plugin = factory->create_plugin(factory, &host, description->id);
    require(plugin && plugin->init(plugin), "CLAP plugin init");
    require(plugin->activate(plugin, 48000, 1, 512), "CLAP activation");
    require(plugin->start_processing(plugin), "CLAP start");
    std::array<clap_event_midi_t, 2> input{};
    for (auto &event : input) {
      event.header.size = sizeof(event);
      event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
      event.header.type = CLAP_EVENT_MIDI;
      event.port_index = 0;
    }
    input[0].header.time = 17;
    input[0].data[0] = 0x90;
    input[0].data[1] = 36;
    input[0].data[2] = 90;
    input[1].header.time = 211;
    input[1].data[0] = 0x80;
    input[1].data[1] = 36;
    clap_input_events_t inputs{};
    inputs.ctx = &input;
    inputs.size = [](const clap_input_events_t *) -> uint32_t { return 2; };
    inputs.get = [](const clap_input_events_t *list, uint32_t i) -> const clap_event_header_t * {
      return &static_cast<const std::array<clap_event_midi_t, 2> *>(list->ctx)->at(i).header;
    };
    struct Seen {
      int note, time;
      bool on;
    };
    std::vector<Seen> events;
    clap_output_events_t outputs{};
    outputs.ctx = &events;
    outputs.try_push = [](const clap_output_events_t *list,
                          const clap_event_header_t *header) -> bool {
      auto &found = *static_cast<std::vector<Seen> *>(list->ctx);
      if (header->space_id != CLAP_CORE_EVENT_SPACE_ID) return true;
      if (header->type == CLAP_EVENT_MIDI) {
        auto *e = reinterpret_cast<const clap_event_midi_t *>(header);
        const int status = e->data[0] & 0xf0;
        if (status == 0x90 || status == 0x80)
          found.push_back(
              {e->data[1], static_cast<int>(header->time), status == 0x90 && e->data[2] != 0});
      } else if (header->type == CLAP_EVENT_NOTE_ON || header->type == CLAP_EVENT_NOTE_OFF) {
        auto *e = reinterpret_cast<const clap_event_note_t *>(header);
        found.push_back(
            {e->key, static_cast<int>(header->time), header->type == CLAP_EVENT_NOTE_ON});
      }
      return true;
    };
    float left[512]{}, right[512]{};
    float *channels[]{left, right};
    clap_audio_buffer_t audio{};
    audio.data32 = channels;
    audio.channel_count = 2;
    clap_process_t process{};
    process.frames_count = 512;
    process.audio_outputs = &audio;
    process.audio_outputs_count = 1;
    process.in_events = &inputs;
    process.out_events = &outputs;
    const auto status = plugin->process(plugin, &process);
    require(status != CLAP_PROCESS_ERROR, "CLAP process");
    require(events.size() == 6, "Expected three note-ons and three note-offs from CLAP");
    for (int i = 0; i < 6; ++i) {
      const auto e = events[static_cast<size_t>(i)];
      require(e.note == 36 || e.note == 40 || e.note == 43, "CLAP chord notes");
      require(e.time == (e.on ? 17 : 211), "CLAP sample offsets");
    }
    const auto *params =
        static_cast<const clap_plugin_params_t *>(plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    require(params, "CLAP parameters");
    auto parameterId = [&](const char *name) {
      for (uint32_t i = 0; i < params->count(plugin); ++i) {
        clap_param_info_t info{};
        if (params->get_info(plugin, i, &info) && std::strcmp(info.name, name) == 0) return info.id;
      }
      throw std::runtime_error("Missing CLAP parameter");
    };
    require(parameterId("Bypass") != CLAP_INVALID_ID, "CLAP Bypass parameter exists");
    require(parameterId("DAW Sync") != CLAP_INVALID_ID, "CLAP DAW Sync parameter exists");
    require(parameterId("Inversion") != CLAP_INVALID_ID &&
                parameterId("Voicing") != CLAP_INVALID_ID,
            "CLAP exposes original inversion and new voicing parameters");
    {
      char text[64]{};
      require(params->value_to_text(plugin, parameterId("Scale"), 1.0, text, sizeof(text)) &&
                  std::strcmp(text, "Iwato") == 0,
              "CLAP exposes final new scale");
    }
    const std::array<const char *, 12> pitches{
        {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}};
    for (int i = 0; i < 12; ++i) {
      char text[32]{};
      require(params->value_to_text(plugin, parameterId("Key"), static_cast<double>(i) / 11, text,
                                    sizeof(text)) &&
                  std::strcmp(text, pitches[static_cast<size_t>(i)]) == 0,
              "CLAP Key pitch text");
    }
    std::array<clap_event_param_value_t, 2> changes{};
    for (auto &e : changes) {
      e.header.size = sizeof(e);
      e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
      e.header.type = CLAP_EVENT_PARAM_VALUE;
      e.note_id = -1;
      e.port_index = -1;
      e.channel = -1;
      e.key = -1;
      e.value = 1;
    }
    changes[0].param_id = parameterId("Mode");
    changes[1].param_id = parameterId("Latch");
    clap_input_events_t values{};
    values.ctx = &changes;
    values.size = [](const clap_input_events_t *) -> uint32_t { return 2; };
    values.get = [](const clap_input_events_t *list, uint32_t i) -> const clap_event_header_t * {
      return &static_cast<const std::array<clap_event_param_value_t, 2> *>(list->ctx)->at(i).header;
    };
    params->flush(plugin, &values, &outputs);
    clap_event_transport_t transport{};
    transport.header.size = sizeof(transport);
    transport.header.type = CLAP_EVENT_TRANSPORT;
    transport.flags =
        CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE | CLAP_TRANSPORT_IS_PLAYING;
    transport.tempo = 120;
    process.transport = &transport;
    input[0].header.time = 0;
    input[0].data[1] = 38;
    input[1].data[1] = 38;
    events.clear();
    plugin->process(plugin, &process);
    bool performed = false;
    inputs.size = [](const clap_input_events_t *) -> uint32_t { return 0; };
    for (int block = 0; block < 500; ++block) {
      for (const auto e : events)
        if (e.on) {
          require(e.note % 12 == 2 || e.note % 12 == 5 || e.note % 12 == 9,
                  "CLAP Performance diatonic D-F-A");
          performed = true;
        }
      events.clear();
      transport.song_pos_beats =
          static_cast<clap_beattime>((block + 1) * 512.0 / 24000 * CLAP_BEATTIME_FACTOR);
      require(plugin->process(plugin, &process) != CLAP_PROCESS_ERROR, "CLAP Performance process");
    }
    require(performed, "CLAP Performance emits real pattern MIDI");
    transport.flags &= ~CLAP_TRANSPORT_IS_PLAYING;
    events.clear();
    plugin->process(plugin, &process);
    for (int block = 0; block < 16; ++block) {
      events.clear();
      plugin->process(plugin, &process);
      for (const auto e : events)
        require(!e.on, "CLAP transport stop cancels pattern");
    }
    changes[1].value = 0;
    params->flush(plugin, &values, &outputs);
    inputs.size = [](const clap_input_events_t *) -> uint32_t { return 2; };
    input[0].header.time = 17;
    events.clear();
    require(plugin->process(plugin, &process) != CLAP_PROCESS_ERROR, "CLAP stopped-key process");
    bool stoppedPlayed = false, stoppedReleased = false;
    for (const auto e : events) {
      if (e.on) {
        stoppedPlayed = true;
        require(e.time == 17, "CLAP stopped-key immediate start");
        require(e.note % 12 == 2 || e.note % 12 == 5 || e.note % 12 == 9,
                "CLAP stopped-key harmony");
      } else {
        stoppedReleased = true;
        require(e.time == 211, "CLAP stopped-key immediate release");
      }
    }
    require(stoppedPlayed && stoppedReleased,
            "CLAP held-key Performance works without transport or Preview");
    require(parameterId("Category") != CLAP_INVALID_ID, "CLAP Category parameter exists");
    changes[0].param_id = parameterId("Performance playback");
    changes[0].value = 1;
    params->flush(plugin, &values, &outputs);
    inputs.size = [](const clap_input_events_t *) -> uint32_t { return 0; };
    events.clear();
    plugin->process(plugin, &process);
    bool remotePreview = false;
    for (const auto e : events)
      remotePreview |= e.on;
    require(remotePreview, "CLAP Remote Control playback starts audition");
    changes[0].value = 0;
    params->flush(plugin, &values, &outputs);
    events.clear();
    plugin->process(plugin, &process);
    for (int block = 0; block < 16; ++block) {
      events.clear();
      plugin->process(plugin, &process);
      for (const auto e : events)
        require(!e.on, "CLAP Remote Control playback stops future events");
    }
    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    entry->deinit();
    std::cout << "CLAP loaded: sample-accurate C-E-G and transport sync and "
                 "stopped-key diatonic Performance passed.\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
