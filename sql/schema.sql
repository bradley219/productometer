create database inotify;
use inotify;
-- MySQL dump 10.13  Distrib 5.5.13, for Linux (x86_64)
--
-- Host: localhost    Database: inotify
-- ------------------------------------------------------
-- Server version	5.5.13-log

/*!40101 SET @OLD_CHARACTER_SET_CLIENT=@@CHARACTER_SET_CLIENT */;
/*!40101 SET @OLD_CHARACTER_SET_RESULTS=@@CHARACTER_SET_RESULTS */;
/*!40101 SET @OLD_COLLATION_CONNECTION=@@COLLATION_CONNECTION */;
/*!40101 SET NAMES utf8 */;
/*!40103 SET @OLD_TIME_ZONE=@@TIME_ZONE */;
/*!40103 SET TIME_ZONE='+00:00' */;
/*!40014 SET @OLD_UNIQUE_CHECKS=@@UNIQUE_CHECKS, UNIQUE_CHECKS=0 */;
/*!40014 SET @OLD_FOREIGN_KEY_CHECKS=@@FOREIGN_KEY_CHECKS, FOREIGN_KEY_CHECKS=0 */;
/*!40101 SET @OLD_SQL_MODE=@@SQL_MODE, SQL_MODE='NO_AUTO_VALUE_ON_ZERO' */;
/*!40111 SET @OLD_SQL_NOTES=@@SQL_NOTES, SQL_NOTES=0 */;

--
-- Table structure for table `computers`
--

DROP TABLE IF EXISTS `computers`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `computers` (
  `computerID` int(11) NOT NULL AUTO_INCREMENT,
  `computer` varchar(128) COLLATE ascii_bin NOT NULL,
  PRIMARY KEY (`computerID`),
  UNIQUE KEY `computer` (`computer`)
) ENGINE=InnoDB AUTO_INCREMENT=2 DEFAULT CHARSET=ascii COLLATE=ascii_bin;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `computers`
--

LOCK TABLES `computers` WRITE;
/*!40000 ALTER TABLE `computers` DISABLE KEYS */;
INSERT INTO `computers` VALUES (1,'i7');
/*!40000 ALTER TABLE `computers` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `events`
--

DROP TABLE IF EXISTS `events`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `events` (
  `eventID` bigint(20) NOT NULL AUTO_INCREMENT,
  `setID` int(11) DEFAULT NULL,
  `fileID` bigint(20) DEFAULT NULL,
  `mask` int(10) unsigned NOT NULL,
  `time` datetime NOT NULL,
  `usec` int(10) unsigned NOT NULL,
  PRIMARY KEY (`eventID`),
  KEY `fileID` (`fileID`,`mask`,`time`,`usec`),
  KEY `setID` (`setID`)
) ENGINE=InnoDB AUTO_INCREMENT=1029 DEFAULT CHARSET=ascii COLLATE=ascii_bin;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `events`
--

LOCK TABLES `events` WRITE;
/*!40000 ALTER TABLE `events` DISABLE KEYS */;
/*!40000 ALTER TABLE `events` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `files`
--

DROP TABLE IF EXISTS `files`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `files` (
  `fileID` bigint(20) NOT NULL AUTO_INCREMENT,
  `computerID` int(11) NOT NULL,
  `file` varchar(763) COLLATE ascii_bin NOT NULL,
  PRIMARY KEY (`fileID`),
  UNIQUE KEY `uniq` (`computerID`,`file`)
) ENGINE=InnoDB AUTO_INCREMENT=960 DEFAULT CHARSET=ascii COLLATE=ascii_bin;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `files`
--

LOCK TABLES `files` WRITE;
/*!40000 ALTER TABLE `files` DISABLE KEYS */;
/*!40000 ALTER TABLE `files` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Temporary table structure for view `joined`
--

DROP TABLE IF EXISTS `joined`;
/*!50001 DROP VIEW IF EXISTS `joined`*/;
SET @saved_cs_client     = @@character_set_client;
SET character_set_client = utf8;
/*!50001 CREATE TABLE `joined` (
  `eventID` bigint(20),
  `computer` varchar(128),
  `set` varchar(128),
  `file` varchar(763),
  `mask` int(10) unsigned,
  `time` datetime,
  `usec` int(10) unsigned
) ENGINE=MyISAM */;
SET character_set_client = @saved_cs_client;

--
-- Table structure for table `sets`
--

DROP TABLE IF EXISTS `sets`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `sets` (
  `setID` int(11) NOT NULL AUTO_INCREMENT,
  `set` varchar(128) COLLATE ascii_bin NOT NULL,
  PRIMARY KEY (`setID`),
  UNIQUE KEY `set` (`set`)
) ENGINE=InnoDB AUTO_INCREMENT=23 DEFAULT CHARSET=ascii COLLATE=ascii_bin;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `sets`
--

LOCK TABLES `sets` WRITE;
/*!40000 ALTER TABLE `sets` DISABLE KEYS */;
INSERT INTO `sets` VALUES (22,'debugging'),(2,'devel'),(3,'logs'),(1,'productometer');
/*!40000 ALTER TABLE `sets` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Final view structure for view `joined`
--

/*!50001 DROP TABLE IF EXISTS `joined`*/;
/*!50001 DROP VIEW IF EXISTS `joined`*/;
/*!50001 SET @saved_cs_client          = @@character_set_client */;
/*!50001 SET @saved_cs_results         = @@character_set_results */;
/*!50001 SET @saved_col_connection     = @@collation_connection */;
/*!50001 SET character_set_client      = utf8 */;
/*!50001 SET character_set_results     = utf8 */;
/*!50001 SET collation_connection      = utf8_general_ci */;
/*!50001 CREATE ALGORITHM=UNDEFINED */
/*!50013 DEFINER=`brsnyder`@`%` SQL SECURITY DEFINER */
/*!50001 VIEW `joined` AS select `events`.`eventID` AS `eventID`,`computers`.`computer` AS `computer`,`sets`.`set` AS `set`,`files`.`file` AS `file`,`events`.`mask` AS `mask`,`events`.`time` AS `time`,`events`.`usec` AS `usec` from (`computers` join (`files` join (`sets` join `events` on((`sets`.`setID` = `events`.`setID`))) on((`files`.`fileID` = `events`.`fileID`))) on((`computers`.`computerID` = `files`.`computerID`))) order by `events`.`eventID` */;
/*!50001 SET character_set_client      = @saved_cs_client */;
/*!50001 SET character_set_results     = @saved_cs_results */;
/*!50001 SET collation_connection      = @saved_col_connection */;
/*!40103 SET TIME_ZONE=@OLD_TIME_ZONE */;

/*!40101 SET SQL_MODE=@OLD_SQL_MODE */;
/*!40014 SET FOREIGN_KEY_CHECKS=@OLD_FOREIGN_KEY_CHECKS */;
/*!40014 SET UNIQUE_CHECKS=@OLD_UNIQUE_CHECKS */;
/*!40101 SET CHARACTER_SET_CLIENT=@OLD_CHARACTER_SET_CLIENT */;
/*!40101 SET CHARACTER_SET_RESULTS=@OLD_CHARACTER_SET_RESULTS */;
/*!40101 SET COLLATION_CONNECTION=@OLD_COLLATION_CONNECTION */;
/*!40111 SET SQL_NOTES=@OLD_SQL_NOTES */;

-- Dump completed on 2011-08-17  9:06:41
