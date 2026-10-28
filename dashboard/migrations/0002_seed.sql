INSERT INTO settings (id, first_phrase_time, second_phrase_time) VALUES (1, '04:00', '16:00');

INSERT INTO display_state (id) VALUES (1);

INSERT INTO special_days (kind, month, day, greeting, base_year, icon, rank) VALUES
  ('yearly', 4, 3, '生日快乐', NULL, 'cake', 10),
  ('yearly', 7, 6, '生日快乐', NULL, 'cake', 10),
  ('yearly', 10, 9, '乔乔生日快乐', NULL, 'cat', 20),
  ('yearly', 9, 3, '伊伊生日快乐', NULL, 'cat', 20),
  ('yearly', 9, 1, '{n}周年', 2021, 'heart', 30),
  ('yearly', 4, 2, '结婚{n}周年', 2026, 'rings', 30),
  ('yearly', 2, 14, '情人节快乐', NULL, 'hearts', 50),
  ('yearly', 5, 20, '520快乐', NULL, 'hearts', 50),
  ('lunar_yearly', 7, 7, '七夕快乐', NULL, 'hearts', 50);
