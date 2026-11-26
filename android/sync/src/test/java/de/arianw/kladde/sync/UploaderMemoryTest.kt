package de.arianw.kladde.sync

/** The upload behaviour against the in-memory stand-in. */
class UploaderMemoryTest : UploaderTest() {
    override fun newStore(): QueueStore = MemoryStore()
}
