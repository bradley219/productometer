USE inotify;

DROP VIEW IF EXISTS joined;
CREATE VIEW joined AS
select 
`events`.`eventID` AS `eventID`,
`computers`.`computer` AS `computer`,
`sets`.`set` AS `set`,
`files`.`file` AS `file`,
-- IF( (`events`.`mask` & 0x00000001), 1, 0 ) AS `IN_ACCESS`, /* File was accessed.  */
IF( (`events`.`mask` & 0x00000002), 1, 0 ) AS `IN_MODIFY`, /* File was modified.  */
-- IF( (`events`.`mask` & 0x00000004), 1, 0 ) AS `IN_ATTRIB`, /* Metadata changed.  */
-- IF( (`events`.`mask` & 0x00000008), 1, 0 ) AS `IN_CLOSE_WRITE`, /* Writtable file was closed.  */
-- IF( (`events`.`mask` & 0x00000010), 1, 0 ) AS `IN_CLOSE_NOWRITE`, /* Unwrittable file closed.  */
-- IF( (`events`.`mask` & 0x00000020), 1, 0 ) AS `IN_OPEN`, /* File was opened.  */
-- IF( (`events`.`mask` & 0x00000040), 1, 0 ) AS `IN_MOVED_FROM`, /* File was moved from X.  */
-- IF( (`events`.`mask` & 0x00000080), 1, 0 ) AS `IN_MOVED_TO`, /* File was moved to Y.  */
IF( (`events`.`mask` & 0x00000100), 1, 0 ) AS `IN_CREATE`, /* Subfile was created.  */
-- IF( (`events`.`mask` & 0x00000200), 1, 0 ) AS `IN_DELETE`, /* Subfile was deleted.  */
-- IF( (`events`.`mask` & 0x00000400), 1, 0 ) AS `IN_DELETE_SELF`, /* Self was deleted.  */
-- IF( (`events`.`mask` & 0x00000800), 1, 0 ) AS `IN_MOVE_SELF`, /* Self was moved.  */
IF( (`events`.`mask`) AND NOT (`events`.`mask` & (0x00000100 | 0x00000002) ), 1, 0 ) AS `IN_OTHER`, 
-- `events`.`mask` AS `mask`,
`events`.`time` AS `time`,
`events`.`usec` AS `usec` 
from 
(
	`computers` join (
		`files` join (
			`sets` join `events` on(
				`sets`.`setID` = `events`.`setID`
			)
		) on(
		`files`.`fileID` = `events`.`fileID`
		)
	) on(
	`computers`.`computerID` = `files`.`computerID`
	)
) 
order by 
`events`.`eventID`;
