enum request {
    NEW_PEER,
    DELETE_PEER,
    NEW_METADATA,
    DELETE_METADATA,
    EXIST_METADATA, // -> |char| 0 - neexistuje
    PEER_LIST,      // -> |int (size_peerlist)|N x char[4](IP) + char (pole)|
    METADATA
};
