# UDP multicast

Complete the [CLI quick start](../quick-start.md) to build `graphx` and prepare your environment.
Run from the repository root:

```sh
graphx example plan udp-multicast
graphx example up udp-multicast
graphx example status udp-multicast
graphx example tokens udp-multicast
graphx example logs udp-multicast --follow
graphx example down udp-multicast
```

The CLI opens an authenticated console automatically. Use `graphx example open udp-multicast` to reopen it without restarting.
Control is disabled unless explicitly granted or enabled in the authored graph.


The CLI prepares required verified artifacts and remembers compilation and state
paths. Use `--images DIR` or `--release DIR` to reuse existing verified artifacts;
Lima inputs refer to guest-local paths. Use `--restart` after changing source or
control selection. Cleanup removes only identity-owned resources and retains history.
