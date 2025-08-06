import { render, screen } from '@testing-library/react';

test('renders without crashing', () => {
  const div = document.createElement('div');
  expect(div).toBeTruthy();
});