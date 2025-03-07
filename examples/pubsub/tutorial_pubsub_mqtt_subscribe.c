/* This work is licensed under a Creative Commons CCZero 1.0 Universal License.
 * See http://creativecommons.org/publicdomain/zero/1.0/ for more information.
 *
 * Copyright (c) 2022 Fraunhofer IOSB (Author: Noel Graf)
 */

#include <open62541/plugin/log_stdout.h>
#include <open62541/server.h>
#include <open62541/server_config_default.h>

#include <signal.h>

#include <open62541/plugin/pubsub_mqtt.h>
#include "ua_pubsub_manager.h"

#define CONNECTION_NAME               "MQTT Subscriber Connection"
#define TRANSPORT_PROFILE_URI         "http://opcfoundation.org/UA-Profile/Transport/pubsub-mqtt"
#define MQTT_CLIENT_ID                "TESTCLIENTPUBSUBMQTTSUBSCRIBE"
#define CONNECTIONOPTION_NAME         "mqttClientId"
#define SUBSCRIBER_TOPIC              "customTopic"
#define SUBSCRIBER_METADATAQUEUENAME  "MetaDataTopic"
#define SUBSCRIBER_METADATAUPDATETIME 0
#define BROKER_ADDRESS_URL            "opc.mqtt://127.0.0.1:1883"
#define SUBSCRIBE_INTERVAL            500

// Uncomment the following line to enable MQTT login for the example
// #define EXAMPLE_USE_MQTT_LOGIN

#ifdef EXAMPLE_USE_MQTT_LOGIN
#define LOGIN_OPTION_COUNT           2
#define USERNAME_OPTION_NAME         "mqttUsername"
#define PASSWORD_OPTION_NAME         "mqttPassword"
#define MQTT_USERNAME                "open62541user"
#define MQTT_PASSWORD                "open62541"
#endif

// Uncomment the following line to enable MQTT via TLS for the example
//#define EXAMPLE_USE_MQTT_TLS

#ifdef EXAMPLE_USE_MQTT_TLS
#define TLS_OPTION_COUNT                2
#define USE_TLS_OPTION_NAME             "mqttUseTLS"
#define MQTT_CA_FILE_PATH_OPTION_NAME   "mqttCaFilePath"
#define CA_FILE_PATH                    "/path/to/server.cert"
#endif

#ifdef UA_ENABLE_JSON_ENCODING
static UA_Boolean useJson = true;
#else
static UA_Boolean useJson = false;
#endif

UA_NodeId connectionIdent;
UA_NodeId subscribedDataSetIdent;
UA_NodeId readerGroupIdent;

UA_DataSetReaderConfig readerConfig;

static void fillTestDataSetMetaData(UA_DataSetMetaDataType *pMetaData);

static UA_StatusCode
addPubSubConnection(UA_Server *server, char *addressUrl) {
    UA_StatusCode retval = UA_STATUSCODE_GOOD;
    /* Details about the connection configuration and handling are located
     * in the pubsub connection tutorial */
    UA_PubSubConnectionConfig connectionConfig;
    memset(&connectionConfig, 0, sizeof(connectionConfig));
    connectionConfig.name = UA_STRING(CONNECTION_NAME);
    connectionConfig.transportProfileUri = UA_STRING(TRANSPORT_PROFILE_URI);
    connectionConfig.enabled = UA_TRUE;

    /* configure address of the mqtt broker (local on default port) */
    UA_NetworkAddressUrlDataType networkAddressUrl = {UA_STRING_NULL , UA_STRING(addressUrl)};
    UA_Variant_setScalar(&connectionConfig.address, &networkAddressUrl,
                         &UA_TYPES[UA_TYPES_NETWORKADDRESSURLDATATYPE]);
    /* Changed to static publisherId from random generation to identify
     * the publisher on Subscriber side */
    connectionConfig.publisherIdType = UA_PUBLISHERIDTYPE_UINT16;
    connectionConfig.publisherId.uint16 = 2234;

    /* configure options, set mqtt client id */
    const int connectionOptionsCount = 1
#ifdef EXAMPLE_USE_MQTT_LOGIN
    + LOGIN_OPTION_COUNT
#endif
#ifdef EXAMPLE_USE_MQTT_TLS
    + TLS_OPTION_COUNT
#endif
    ;

    UA_KeyValuePair connectionOptions[connectionOptionsCount];

    size_t connectionOptionIndex = 0;
    connectionOptions[connectionOptionIndex].key = UA_QUALIFIEDNAME(0, CONNECTIONOPTION_NAME);
    UA_String mqttClientId = UA_STRING(MQTT_CLIENT_ID);
    UA_Variant_setScalar(&connectionOptions[connectionOptionIndex++].value, &mqttClientId, &UA_TYPES[UA_TYPES_STRING]);

#ifdef EXAMPLE_USE_MQTT_LOGIN
    connectionOptions[connectionOptionIndex].key = UA_QUALIFIEDNAME(0, USERNAME_OPTION_NAME);
    UA_String mqttUsername = UA_STRING(MQTT_USERNAME);
    UA_Variant_setScalar(&connectionOptions[connectionOptionIndex++].value, &mqttUsername, &UA_TYPES[UA_TYPES_STRING]);

    connectionOptions[connectionOptionIndex].key = UA_QUALIFIEDNAME(0, PASSWORD_OPTION_NAME);
    UA_String mqttPassword = UA_STRING(MQTT_PASSWORD);
    UA_Variant_setScalar(&connectionOptions[connectionOptionIndex++].value, &mqttPassword, &UA_TYPES[UA_TYPES_STRING]);
#endif

#ifdef EXAMPLE_USE_MQTT_TLS
    connectionOptions[connectionOptionIndex].key = UA_QUALIFIEDNAME(0, USE_TLS_OPTION_NAME);
    UA_Boolean mqttUseTLS = true;
    UA_Variant_setScalar(&connectionOptions[connectionOptionIndex++].value, &mqttUseTLS, &UA_TYPES[UA_TYPES_BOOLEAN]);

    connectionOptions[connectionOptionIndex].key = UA_QUALIFIEDNAME(0, MQTT_CA_FILE_PATH_OPTION_NAME);
    UA_String mqttCaFile = UA_STRING(CA_FILE_PATH);
    UA_Variant_setScalar(&connectionOptions[connectionOptionIndex++].value, &mqttCaFile, &UA_TYPES[UA_TYPES_STRING]);
#endif

    connectionConfig.connectionProperties = connectionOptions;
    connectionConfig.connectionPropertiesSize = connectionOptionIndex;

    retval |= UA_Server_addPubSubConnection(server, &connectionConfig, &connectionIdent);

    return retval;
}

/**
 * **ReaderGroup**
 *
 * ReaderGroup is used to group a list of DataSetReaders. All ReaderGroups are
 * created within a PubSubConnection and automatically deleted if the connection
 * is removed. All network message related filters are only available in the DataSetReader. */
/* Add ReaderGroup to the created connection */
static UA_StatusCode
addReaderGroup(UA_Server *server,  int interval) {
    if(server == NULL) {
        return UA_STATUSCODE_BADINTERNALERROR;
    }

    UA_StatusCode retval = UA_STATUSCODE_GOOD;
    UA_ReaderGroupConfig readerGroupConfig;
    memset (&readerGroupConfig, 0, sizeof(UA_ReaderGroupConfig));
    readerGroupConfig.name = UA_STRING("ReaderGroup1");
    readerGroupConfig.subscribingInterval = interval;
    if(useJson)
        readerGroupConfig.encodingMimeType = UA_PUBSUB_ENCODING_JSON;

    /* configure the mqtt publish topic */
    UA_BrokerWriterGroupTransportDataType brokerTransportSettings;
    memset(&brokerTransportSettings, 0, sizeof(UA_BrokerWriterGroupTransportDataType));
    /* Assign the Topic at which MQTT publish should happen */
    /*ToDo: Pass the topic as argument from the reader group */
    brokerTransportSettings.queueName = UA_STRING(SUBSCRIBER_TOPIC);
    brokerTransportSettings.resourceUri = UA_STRING_NULL;
    brokerTransportSettings.authenticationProfileUri = UA_STRING_NULL;

    /* Choose the QOS Level for MQTT */
    brokerTransportSettings.requestedDeliveryGuarantee = UA_BROKERTRANSPORTQUALITYOFSERVICE_BESTEFFORT;

    /* Encapsulate config in transportSettings */
    UA_ExtensionObject transportSettings;
    memset(&transportSettings, 0, sizeof(UA_ExtensionObject));
    transportSettings.encoding = UA_EXTENSIONOBJECT_DECODED;
    transportSettings.content.decoded.type = &UA_TYPES[UA_TYPES_BROKERDATASETREADERTRANSPORTDATATYPE];
    transportSettings.content.decoded.data = &brokerTransportSettings;

    readerGroupConfig.transportSettings = transportSettings;

    retval |= UA_Server_addReaderGroup(server, connectionIdent, &readerGroupConfig,
                                       &readerGroupIdent);
    retval |= UA_Server_setReaderGroupOperational(server, readerGroupIdent);

    return retval;
}

/**
 * **DataSetReader**
 *
 * DataSetReader can receive NetworkMessages with the DataSetMessage
 * of interest sent by the Publisher. DataSetReader provides
 * the configuration necessary to receive and process DataSetMessages
 * on the Subscriber side. DataSetReader must be linked with a
 * SubscribedDataSet and be contained within a ReaderGroup. */
/* Add DataSetReader to the ReaderGroup */
static UA_StatusCode
addDataSetReader(UA_Server *server) {
    if(server == NULL) {
        return UA_STATUSCODE_BADINTERNALERROR;
    }

    UA_StatusCode retval = UA_STATUSCODE_GOOD;
    memset (&readerConfig, 0, sizeof(UA_DataSetReaderConfig));
    readerConfig.name = UA_STRING("DataSet Reader 1");
    /* Parameters to filter which DataSetMessage has to be processed
     * by the DataSetReader */
    /* The following parameters are used to show that the data published by
     * tutorial_pubsub_mqtt_publish.c is being subscribed and is being updated in
     * the information model */
    UA_UInt16 publisherIdentifier = 2234;
    readerConfig.publisherId.type = &UA_TYPES[UA_TYPES_UINT16];
    readerConfig.publisherId.data = &publisherIdentifier;
    readerConfig.writerGroupId    = 100;
    readerConfig.dataSetWriterId  = 62541;
#ifdef UA_ENABLE_PUBSUB_MONITORING
    readerConfig.messageReceiveTimeout = 0;
#endif

    /* Setting up Meta data configuration in DataSetReader */
    fillTestDataSetMetaData(&readerConfig.dataSetMetaData);

    retval |= UA_Server_addDataSetReader(server, readerGroupIdent, &readerConfig,
                                         &subscribedDataSetIdent);
    return retval;
}

/**
 * **SubscribedDataSet**
 *
 * Set SubscribedDataSet type to TargetVariables data type.
 * Add subscribedvariables to the DataSetReader */
static UA_StatusCode
addSubscribedVariables (UA_Server *server, UA_NodeId dataSetReaderId) {
    if(server == NULL)
        return UA_STATUSCODE_BADINTERNALERROR;

    UA_StatusCode retval = UA_STATUSCODE_GOOD;
    UA_NodeId folderId;
    UA_String folderName = readerConfig.dataSetMetaData.name;
    UA_ObjectAttributes oAttr = UA_ObjectAttributes_default;
    UA_QualifiedName folderBrowseName;
    if(folderName.length > 0) {
        oAttr.displayName.locale = UA_STRING ("en-US");
        oAttr.displayName.text = folderName;
        folderBrowseName.namespaceIndex = 1;
        folderBrowseName.name = folderName;
    }
    else {
        oAttr.displayName = UA_LOCALIZEDTEXT ("en-US", "Subscribed Variables");
        folderBrowseName = UA_QUALIFIEDNAME (1, "Subscribed Variables");
    }

    UA_Server_addObjectNode (server, UA_NODEID_NULL,
                             UA_NODEID_NUMERIC (0, UA_NS0ID_OBJECTSFOLDER),
                             UA_NODEID_NUMERIC (0, UA_NS0ID_ORGANIZES),
                             folderBrowseName, UA_NODEID_NUMERIC (0,
                                                                  UA_NS0ID_BASEOBJECTTYPE), oAttr, NULL, &folderId);

/**
 * **TargetVariables**
 *
 * The SubscribedDataSet option TargetVariables defines a list of Variable mappings between
 * received DataSet fields and target Variables in the Subscriber AddressSpace.
 * The values subscribed from the Publisher are updated in the value field of these variables */
    /* Create the TargetVariables with respect to DataSetMetaData fields */
    UA_FieldTargetVariable *targetVars = (UA_FieldTargetVariable *)
        UA_calloc(readerConfig.dataSetMetaData.fieldsSize, sizeof(UA_FieldTargetVariable));
    for(size_t i = 0; i < readerConfig.dataSetMetaData.fieldsSize; i++) {
        /* Variable to subscribe data */
        UA_VariableAttributes vAttr = UA_VariableAttributes_default;
        UA_LocalizedText_copy(&readerConfig.dataSetMetaData.fields[i].description,
                              &vAttr.description);
        vAttr.displayName.locale = UA_STRING("en-US");
        vAttr.displayName.text = readerConfig.dataSetMetaData.fields[i].name;
        vAttr.dataType = readerConfig.dataSetMetaData.fields[i].dataType;

        UA_NodeId newNode;
        retval |= UA_Server_addVariableNode(server, UA_NODEID_NUMERIC(1, (UA_UInt32)i + 50000),
                                            folderId,
                                            UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
                                            UA_QUALIFIEDNAME(1, (char *)readerConfig.dataSetMetaData.fields[i].name.data),
                                            UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
                                            vAttr, NULL, &newNode);

        /* For creating Targetvariables */
        UA_FieldTargetDataType_init(&targetVars[i].targetVariable);
        targetVars[i].targetVariable.attributeId  = UA_ATTRIBUTEID_VALUE;
        targetVars[i].targetVariable.targetNodeId = newNode;
    }

    retval = UA_Server_DataSetReader_createTargetVariables(server, dataSetReaderId,
                                                           readerConfig.dataSetMetaData.fieldsSize, targetVars);
    for(size_t i = 0; i < readerConfig.dataSetMetaData.fieldsSize; i++)
        UA_FieldTargetDataType_clear(&targetVars[i].targetVariable);

    UA_free(targetVars);
    UA_free(readerConfig.dataSetMetaData.fields);
    return retval;
}

/**
 * **DataSetMetaData**
 *
 * The DataSetMetaData describes the content of a DataSet. It provides the information necessary to decode
 * DataSetMessages on the Subscriber side. DataSetMessages received from the Publisher are decoded into
 * DataSet and each field is updated in the Subscriber based on datatype match of TargetVariable fields of Subscriber
 * and PublishedDataSetFields of Publisher */
/* Define MetaData for TargetVariables */
static void fillTestDataSetMetaData(UA_DataSetMetaDataType *pMetaData) {
    if(pMetaData == NULL) {
        return;
    }

    UA_DataSetMetaDataType_init (pMetaData);
    pMetaData->name = UA_STRING ("DataSet 1");

    /* Static definition of number of fields size to 4 to create four different
     * targetVariables of distinct datatype
     * Currently the publisher sends only DateTime data type */
    pMetaData->fieldsSize = 4;
    pMetaData->fields = (UA_FieldMetaData*)UA_Array_new (pMetaData->fieldsSize,
                                                         &UA_TYPES[UA_TYPES_FIELDMETADATA]);

    /* DateTime DataType */
    UA_FieldMetaData_init (&pMetaData->fields[0]);
    UA_NodeId_copy (&UA_TYPES[UA_TYPES_DATETIME].typeId,
                    &pMetaData->fields[0].dataType);
    pMetaData->fields[0].builtInType = UA_NS0ID_DATETIME;
    pMetaData->fields[0].name =  UA_STRING ("DateTime");
    pMetaData->fields[0].valueRank = -1; /* scalar */

    /* Int32 DataType */
    UA_FieldMetaData_init (&pMetaData->fields[1]);
    UA_NodeId_copy(&UA_TYPES[UA_TYPES_INT32].typeId,
                   &pMetaData->fields[1].dataType);
    pMetaData->fields[1].builtInType = UA_NS0ID_INT32;
    pMetaData->fields[1].name =  UA_STRING ("Int32");
    pMetaData->fields[1].valueRank = -1; /* scalar */

    /* Int64 DataType */
    UA_FieldMetaData_init (&pMetaData->fields[2]);
    UA_NodeId_copy(&UA_TYPES[UA_TYPES_INT64].typeId,
                   &pMetaData->fields[2].dataType);
    pMetaData->fields[2].builtInType = UA_NS0ID_INT64;
    pMetaData->fields[2].name =  UA_STRING ("Int64");
    pMetaData->fields[2].valueRank = -1; /* scalar */

    /* Boolean DataType */
    UA_FieldMetaData_init (&pMetaData->fields[3]);
    UA_NodeId_copy (&UA_TYPES[UA_TYPES_BOOLEAN].typeId,
                    &pMetaData->fields[3].dataType);
    pMetaData->fields[3].builtInType = UA_NS0ID_BOOLEAN;
    pMetaData->fields[3].name =  UA_STRING ("BoolToggle");
    pMetaData->fields[3].valueRank = -1; /* scalar */
}

/**
 * Followed by the main server code, making use of the above definitions */
UA_Boolean running = true;
static void stopHandler(int sign) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "received ctrl-c");
    running = false;
}


int main(int argc, char **argv) {
    signal(SIGINT, stopHandler);
    signal(SIGTERM, stopHandler);

    char *addressUrl = BROKER_ADDRESS_URL;
    //char *topic = SUBSCRIBER_TOPIC;
    int interval = SUBSCRIBE_INTERVAL;

    /* Return value initialized to Status Good */
    UA_StatusCode retval = UA_STATUSCODE_GOOD;
    UA_Server *server = UA_Server_new();
    UA_ServerConfig *config = UA_Server_getConfig(server);
    UA_ServerConfig_setMinimal(config, 4841, NULL);
    UA_ServerConfig_addPubSubTransportLayer(config, UA_PubSubTransportLayerMQTT());

    /* API calls */
    /* Add PubSubConnection */
    retval |= addPubSubConnection(server, addressUrl);
    if (retval != UA_STATUSCODE_GOOD)
        return EXIT_FAILURE;

    /* Add ReaderGroup to the created PubSubConnection */
    retval |= addReaderGroup(server, interval);
    if (retval != UA_STATUSCODE_GOOD)
        return EXIT_FAILURE;

    /* Add DataSetReader to the created ReaderGroup */
    retval |= addDataSetReader(server);
    if (retval != UA_STATUSCODE_GOOD)
        return EXIT_FAILURE;

    /* Add SubscribedVariables to the created DataSetReader */
    retval |= addSubscribedVariables(server, subscribedDataSetIdent);
    if (retval != UA_STATUSCODE_GOOD)
        return EXIT_FAILURE;

    retval = UA_Server_run(server, &running);
    UA_Server_delete(server);
    return retval == UA_STATUSCODE_GOOD ? EXIT_SUCCESS : EXIT_FAILURE;
}
